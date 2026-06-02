I don't have a file-writing tool in this workspace, and your actual source lives on the cluster nodes (`/mirror/img_processor/`), not here — so writing a file into this sandbox wouldn't help you. Instead, here is the complete document ready to save as `/mirror/img_processor/MPI_EXPLAINED.md` (or anywhere you like). Copy the block below verbatim.

````markdown
# How `main_pararell.c` Works — MPI Distributed Image Processing

A deep dive into the parallel architecture of the BMP image processor, with
heavy emphasis on **how MPI launches the processes and how tasks are delegated
across the cluster**.


## 1. What the program does

`main_pararell.c` applies up to six image transformations to a set of BMP files:

| Code | Transformation                          |
|------|-----------------------------------------|
| `vg` | Vertical flip, grayscale                |
| `vc` | Vertical flip, color                    |
| `hg` | Horizontal flip, grayscale              |
| `hc` | Horizontal flip, color                  |
| `dg` | Blur (desenfoque), grayscale            |
| `dc` | Blur (desenfoque), color                |

The work is spread across a **heterogeneous cluster** — in this setup an x86_64
Arch Linux node (`ub0`) and an aarch64 Ubuntu node (`ub2`) — using **MPI**
(Open MPI 5.x / PRTE). Because the two machines run at different speeds, the
program uses a **dynamic master/worker scheduler** so the faster node
automatically does more work.

---

## 2. MPI in one paragraph

MPI (Message Passing Interface) runs **the same binary as many independent
processes** ("ranks"), possibly on different machines, and lets them
communicate by **sending messages** — there is no shared memory between nodes.
Each process gets:

- a **rank**: its unique integer ID (`0 .. world_size-1`), via
  `MPI_Comm_rank`;
- the **world size**: how many processes exist in total, via `MPI_Comm_size`.

Everything else in this program is built on top of those two numbers plus a
handful of send/receive calls.

```c
MPI_Init(&argc, &argv);                      // start the MPI runtime
MPI_Comm_size(MPI_COMM_WORLD, &world_size);  // how many of us are there?
MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);  // which one am I?
```

`MPI_COMM_WORLD` is the default *communicator* — the group containing every
process in the job. All messaging in this program happens inside it.

---

## 3. How the processes get launched (the cluster layer)

You start the job with:

```bash
mpirun \
  --prtemca oob_tcp_if_include tailscale0 \
  --mca btl_tcp_if_include tailscale0 \
  --mca btl_tcp_disable_family 6 \
  --hostfile machinefile \
  execute.sh --transforms vg vc
```

What happens, layer by layer:

1. **`mpirun`** reads `machinefile`:
   ```
   ub0 slots=6
   ub2 slots=6
   ```
   This means "start up to 6 ranks on ub0 and 6 on ub2" → **12 ranks total**.

2. **PRTE** (the runtime behind Open MPI 5.x) launches a **daemon** on each
   node over SSH/Tailscale. The daemons form the **OOB** (out-of-band) control
   channel — this is what `oob_tcp_if_include tailscale0` pins to the Tailscale
   interface.

3. Each daemon spawns its local ranks, all running `execute.sh`, which
   `exec`s the architecture-correct binary
   (`imgprocP_linux_x86` on ub0, `imgprocP_linux_arm` on ub2).

4. The ranks open the **BTL** (byte-transfer layer) — the high-speed
   rank-to-rank data channel — also pinned to Tailscale, IPv4 only
   (`btl_tcp_disable_family 6` avoids an IPv4/IPv6 mismatch between the two
   OSes).

 **Key idea:** by the time `main()` runs, you have 12 *separate* copies of the
 program running, each with a different `world_rank`, all able to message each
 other over Tailscale.


## 4. Program flow (all ranks run this top section identically)

Steps 4.1–4.4 execute on **every rank** — they each parse args, scan the
directory, and read BMP headers independently. This is intentional: it means a
worker never has to be *told* the file paths or image dimensions; it already
has them locally.

### 4.1 Argument parsing
A simple `while` loop over `argv` sets the transform flags (`do_vg`, `do_dc`,
…), kernel sizes (`--kernel-dg`, `--kernel-dc`), and collects image paths.

### 4.2 Default image discovery
If no images are passed, `load_default_images()` scans `img_to_process/` for
`.bmp` files and sorts them (`qsort`) so **every rank builds the identical,
identically-ordered list** — critical, because tasks are later referenced by
*index* into this list.

### 4.3 Validation
If there are no images, or no transform was selected, the program calls
`abort_with_usage()` → `MPI_Abort`. (The famous
`Error: no se seleccionó ninguna transformación` came from here when
`--transforms` was omitted.)

### 4.4 Read BMP headers
`load_bmp_info()` fills a `bmp_image_info` for each image. Again, done on all
ranks, so `bmps[i]` is valid everywhere.

---

## 5. Building the flat task list

The scheduler's unit of work is **one transform on one image**, not a whole
image. This fine granularity is what lets the load balance smoothly.

```c
static image_task tasks[MAX_TASKS];   // MAX_TASKS = 251 * 6
int task_count = 0;
for (int img = 0; img < image_count; img++) {
  if (do_vg) tasks[task_count++] = (image_task){img, TASK_VG, 0};
  if (do_vc) tasks[task_count++] = (image_task){img, TASK_VC, 0};
  if (do_hg) tasks[task_count++] = (image_task){img, TASK_HG, 0};
  if (do_hc) tasks[task_count++] = (image_task){img, TASK_HC, 0};
  if (do_dg) tasks[task_count++] = (image_task){img, TASK_DG, kernel_dg};
  if (do_dc) tasks[task_count++] = (image_task){img, TASK_DC, kernel_dc};
}
```

`tasks[]` is `static` so it lives in the data segment, not the stack (251×6
structs would be large for a stack). Every rank builds this same list, but only
**rank 0 actually uses it** to hand out work.

---

## 6. ⭐ The heart of it: dynamic master/worker delegation

This is where MPI does the real work. There are two roles:

- **Rank 0 = the master (dispatcher).** It holds the task list and hands out
  tasks **on demand**. It processes no images itself.
- **Ranks 1..N = workers.** Each one repeatedly asks for a task, does it, and
  asks again — until told to stop.

Nothing is pushed proactively. It's a **pull model**: a worker can never get a
second task until it comes back for one. That single fact is what makes the
whole thing self-balancing.

### 6.1 The message protocol — three tags

```c
enum { TAG_REQUEST = 1, TAG_WORK = 2, TAG_STOP = 3 };
```

| Tag           | Direction          | Meaning                              |
|---------------|--------------------|--------------------------------------|
| `TAG_REQUEST` | worker → master    | "I'm free, give me work."            |
| `TAG_WORK`    | master → worker    | "Here's a task" (carries payload).   |
| `TAG_STOP`    | master → worker    | "No work left — shut down."          |

The **tag** is how the worker tells a real task apart from a stop signal, even
though both arrive in the same `MPI_Recv`.

### 6.2 What crosses the wire

A task is sent as a **3-integer array**, not the C struct:

```c
msg[0] = tasks[next].image_index;   // which image (index into images[])
msg[1] = (int)tasks[next].kind;     // which transform (TASK_VG ... TASK_DC)
msg[2] = tasks[next].kernel_size;   // blur kernel, 0 when N/A
```

That is the **entire payload — 12 bytes.** **No pixel data is ever
transmitted.** Because every rank already loaded `images[]` and `bmps[]`
(Section 4), the worker only needs the *index* to find its local copy of the
file and header. The message says *which* job, never the data for the job.

> **Why `int[3]` and not the struct?** ub0 is x86_64, ub2 is aarch64. Sending
> three plain `int`s is architecture-safe (no struct padding / layout
> assumptions cross the wire). MPI handles the byte representation; you stay
> portable.

### 6.3 The master loop (rank 0)

```c
int next = 0;                  // index of next task to give out
int active = world_size - 1;   // workers still running
int msg[3];
MPI_Status st;

while (active > 0) {
  // Wait for a request from ANY worker:
  MPI_Recv(msg, 1, MPI_INT, MPI_ANY_SOURCE, TAG_REQUEST, MPI_COMM_WORLD, &st);

  if (next < task_count) {
    // Still have work: pack task `next` and send it to whoever asked.
    msg[0] = tasks[next].image_index;
    msg[1] = (int)tasks[next].kind;
    msg[2] = tasks[next].kernel_size;
    MPI_Send(msg, 3, MPI_INT, st.MPI_SOURCE, TAG_WORK, MPI_COMM_WORLD);
    next++;
  } else {
    // Out of work: tell that worker to stop.
    MPI_Send(msg, 3, MPI_INT, st.MPI_SOURCE, TAG_STOP, MPI_COMM_WORLD);
    active--;
  }
}
```

Three details that make this correct:

1. **`MPI_ANY_SOURCE`** — the master doesn't care *who* asks. Whichever worker
   finishes first is served first. This is the balancing mechanism: ub0's fast
   cores come back for more work sooner, so they naturally receive more tasks.
2. **`st.MPI_SOURCE`** — the receive `status` records *which* rank sent the
   request, so the master knows exactly where to send the reply.
3. **`active` counter** — the master keeps looping until it has sent a
   `TAG_STOP` to **every** worker. Each worker triggers exactly one stop, so
   `active` reaches 0 precisely when all workers have been dismissed.

### 6.4 The worker loop (ranks 1..N)

```c
int msg[3];
MPI_Status st;

for (;;) {
  // 1. Announce availability.
  MPI_Send(&world_rank, 1, MPI_INT, 0, TAG_REQUEST, MPI_COMM_WORLD);

  // 2. Block until the master replies (could be WORK or STOP).
  MPI_Recv(msg, 3, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &st);

  // 3. Was it a stop signal?
  if (st.MPI_TAG == TAG_STOP) break;

  // 4. Rebuild the task and run it on LOCAL data.
  image_task task = {msg[0], (task_kind)msg[1], msg[2]};
  run_task(&task, images, bmps);
  // 5. loop → ask again
}
```

The worker **blocks** inside `MPI_Recv` until the master answers, so an idle
worker consumes no CPU spinning. `MPI_ANY_TAG` lets the one receive call accept
either a `TAG_WORK` or a `TAG_STOP` message; the `st.MPI_TAG` check decides
which it was.

### 6.5 Why this balances automatically

There is **no schedule and no weights.** A node's share of the work emerges
purely from how fast it returns for more:

- ub0 (fast) finishes a task quickly → sends `TAG_REQUEST` again sooner → gets
  another task. Over the run it completes many tasks.
- ub2 (slower) takes longer per task → returns less often → gets fewer tasks.

No node ever sits idle while work remains, and you never had to tell the
program that one machine is faster. **That is the entire fix for the original
"ub0 finishes early and goes idle" problem.**

---

## 7. The single-process fallback

```c
if (world_size == 1) {
  for (int t = 0; t < task_count; t++)
    run_task(&tasks[t], images, bmps);
}
```

With only one rank there are no workers to delegate to, so rank 0 just does all
the tasks itself. This keeps a plain `./imgprocP` (no `mpirun`) working.

---

## 8. Timing and synchronization

```c
MPI_Barrier(MPI_COMM_WORLD);          // all ranks line up before the clock starts
const double start = MPI_Wtime();
   ... work ...
const double local_elapsed = MPI_Wtime() - start;

double max_elapsed = 0.0;
MPI_Reduce(&local_elapsed, &max_elapsed, 1, MPI_DOUBLE,
           MPI_MAX, 0, MPI_COMM_WORLD);
```

- **`MPI_Barrier`** blocks until *every* rank reaches it, so all clocks start
  together and the measurement is fair.
- **`MPI_Wtime`** is a high-resolution wall-clock timer.
- **`MPI_Reduce` with `MPI_MAX`** collapses every rank's elapsed time into a
  single value on rank 0 — the **maximum**, i.e. the slowest finisher, which is
  the true end-to-end runtime of the whole job.

---

## 9. Cleanup and output

```c
for (int img = 0; img < image_count; img++)
  bmp_free_info(&bmps[img]);          // every rank frees its own headers

if (world_rank == 0) {                // only rank 0 prints
  printf("TIEMPO:%.4f\n", max_elapsed);
  printf("THREADS:%d\n", world_size);
}

MPI_Finalize();                       // shut down the MPI runtime — required
```

Only rank 0 prints the `TIEMPO:` / `THREADS:` lines the GUI parses, so you get
one clean result instead of 12. `MPI_Finalize()` must be called by every rank
before exit.

---

## 10. End-to-end example (12 ranks, 10 images, transforms `vg vc`)

- Task list: 10 images × 2 transforms = **20 tasks**.
- Rank 0 = master, ranks 1–11 = 11 workers.
- All 11 workers immediately send `TAG_REQUEST`. The master answers each with
  `TAG_WORK` for tasks 0..10 (`next` advances to 11).
- A fast ub0 worker finishes task #2, sends `TAG_REQUEST` again, gets task #11.
  Meanwhile a slow ub2 worker is still on its first task.
- This continues; fast workers cycle several times. After task #19 is handed
  out, `next == task_count`.
- The next 11 requests each get `TAG_STOP`; `active` drops 11 → 0; master exits
  the loop.
- All ranks meet at `MPI_Reduce`, rank 0 prints the time, everyone
  `MPI_Finalize`s.

Result: ub0 likely processed far more than its "fair share" of the 20 tasks —
exactly what you wanted.

---

## 11. Edge cases & caveats

- **Rank 0 does no image work.** With `slots=6 + slots=6` you run 12 ranks but
  only 11 process images; one ub0 core is spent coordinating. For large task
  counts the dispatcher is nearly idle, so this is a good trade. (It can be
  changed to also process tasks between requests if you want that core back.)
- **No fault tolerance.** If a worker crashes mid-task, the master will block
  forever waiting on the `active` count, because that worker's `TAG_STOP` never
  happens and its task is never reassigned. Standard MPI has no built-in
  recovery here.
- **`MAX_TASKS = 251 × 6`** bounds the task array. More than 251 images would
  overflow the `images[]`/`tasks[]` limits.
- **Both binaries must be built from this same source.** If the master and
  workers disagree on the protocol (tag values, payload size), messaging
  breaks. Rebuild `imgprocP_linux_x86` (ub0) and `imgprocP_linux_arm` (ub2)
  together.

---

## 12. MPI call reference (everything used in this file)

| Call            | Role in this program                                        |
|-----------------|-------------------------------------------------------------|
| `MPI_Init`      | Start the runtime; must precede all other MPI calls.        |
| `MPI_Comm_size` | Total number of ranks (`world_size`).                       |
| `MPI_Comm_rank` | This process's ID (`world_rank`); selects master vs worker. |
| `MPI_Send`      | Send a tagged message to a specific rank.                   |
| `MPI_Recv`      | Block until a matching (source, tag) message arrives.       |
| `MPI_ANY_SOURCE`| Receive from *any* sender — enables on-demand balancing.    |
| `MPI_ANY_TAG`   | Accept either WORK or STOP in one receive.                  |
| `MPI_Status`    | Carries `MPI_SOURCE` (who sent) and `MPI_TAG` (what kind).   |
| `MPI_Barrier`   | Synchronize all ranks before timing.                        |
| `MPI_Wtime`     | Wall-clock timer.                                           |
| `MPI_Reduce`    | Combine per-rank times into the max on rank 0.              |
| `MPI_Abort`     | Hard-stop the whole job on a fatal input error.             |
| `MPI_Finalize`  | Clean MPI shutdown; required before exit.                  |
````

A couple of notes on the doc:

- I wrote it in **English** to match how you've been communicating, even though your code comments are in Spanish — say the word and I'll translate the whole thing to Spanish.
- The **delegation mechanics (Sections 6.3–6.5)** are the core of what you asked to emphasize: the pull model, `MPI_ANY_SOURCE`, the `active` termination counter, and why it self-balances across ub0/ub2.

Since I couldn't write to disk here, the fastest path is: `cd /mirror/img_processor`, then paste the block into `MPI_EXPLAINED.md`. If you'd rather, I can tailor it further — for example fold in a sequence diagram or trim it to a one-page summary.