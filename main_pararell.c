/*
 * main_pararell.c
 * Procesador de imágenes BMP con paralelismo distribuido por MPI.
 *
 * Uso:
 *   mpirun -np <procesos> [--hostfile machinefile] ./imgprocP \
 *     <img1.bmp> [img2.bmp ...] --transforms <vg|vc|hg|hc|dg|dc> [...] \
 *     [--kernel-dg N] [--kernel-dc N]
 *
 * Acrónimos de transformación:
 *   vg  -> inversión vertical escala de grises
 *   vc  -> inversión vertical a colores
 *   hg  -> inversión horizontal escala de grises
 *   hc  -> inversión horizontal a colores
 *   dg  -> desenfoque escala de grises
 *   dc  -> desenfoque a colores
 */

#include "functions/bmp_utils.h"
#include "functions/desenfoque.h"
#include "functions/gris.h"
#include "functions/inv_hz.h"
#include "functions/inv_vt.h"
#include <dirent.h>
#include <limits.h>
#include <mpi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_IMAGES 600
#define MAX_TASKS (MAX_IMAGES * 6)

/* ─── Buffers para Stdout y Stderr para el archivo de log ─── */
static char   stdout_buffer[65536] = "";
static size_t stdout_buffer_len    = 0;

static void log_printf(const char *format, ...) {
  va_list args1, args2;
  va_start(args1, format);
  va_copy(args2, args1);

  // Imprimir a stdout real
  vprintf(format, args1);
  va_end(args1);

  // Guardar en buffer
  int written = vsnprintf(stdout_buffer + stdout_buffer_len,
                          sizeof(stdout_buffer) - stdout_buffer_len,
                          format,
                          args2);
  va_end(args2);

  if (written > 0) {
    stdout_buffer_len += written;
    if (stdout_buffer_len >= sizeof(stdout_buffer)) {
      stdout_buffer_len = sizeof(stdout_buffer) - 1;
    }
  }
}

static char   stderr_buffer[16384] = "";
static size_t stderr_buffer_len    = 0;

static void log_stderr_printf(const char *format, ...) {
  va_list args1, args2;
  va_start(args1, format);
  va_copy(args2, args1);

  // Imprimir a stderr real
  vfprintf(stderr, format, args1);
  va_end(args1);

  // Guardar en buffer
  int written = vsnprintf(stderr_buffer + stderr_buffer_len,
                          sizeof(stderr_buffer) - stderr_buffer_len,
                          format,
                          args2);
  va_end(args2);

  if (written > 0) {
    stderr_buffer_len += written;
    if (stderr_buffer_len >= sizeof(stderr_buffer)) {
      stderr_buffer_len = sizeof(stderr_buffer) - 1;
    }
  }
}

/* ─── Estructura de reporte del trabajador ─── */
typedef struct __attribute__((packed)) {
  int    rank;
  int    completed_task_id; // ID de tarea (1-based), 0 si es el primer request
  double elapsed;
  char   host[64];
} worker_report;

/* ─── Estructuras para parseo de hosts ─── */
typedef struct {
  char host[128];
  int  slots;
} host_info;

static host_info active_hosts[100];
static int       active_hosts_count = 0;

static void parse_machinefile(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) {
    return;
  }

  char line[256];
  while (fgets(line, sizeof(line), f)) {
    // Recortar espacios
    char *start = line;
    while (*start == ' ' || *start == '\t' || *start == '\r'
           || *start == '\n') {
      start++;
    }
    char *end = start + strlen(start) - 1;
    while (end > start
           && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
      end--;
    }
    if (end >= start) {
      *(end + 1) = '\0';
    } else {
      continue;
    }

    if (start[0] == '\0' || start[0] == '#') {
      continue;
    }

    char  host[128] = "";
    int   slots     = 1;
    char *space     = strpbrk(start, " \t");
    if (space) {
      *space = '\0';
      strncpy(host, start, sizeof(host) - 1);
      char *slots_ptr = strstr(space + 1, "slots=");
      if (slots_ptr) {
        slots = atoi(slots_ptr + 6);
        if (slots <= 0)
          slots = 1;
      }
    } else {
      strncpy(host, start, sizeof(host) - 1);
    }

    if (active_hosts_count < 100) {
      strncpy(active_hosts[active_hosts_count].host,
              host,
              sizeof(active_hosts[active_hosts_count].host) - 1);
      active_hosts[active_hosts_count].slots = slots;
      active_hosts_count++;
    }
  }
  fclose(f);
}

/* ─── flags de transformación (globales de solo lectura en runtime) ─── */
static int do_vg = 0, do_vc = 0, do_hg = 0, do_hc = 0, do_dg = 0, do_dc = 0;
static int kernel_dg = 16, kernel_dc = 16;

static void write_execution_log(time_t             started_time,
                                time_t             finished_time,
                                double             elapsed,
                                int                world_size,
                                int                task_count,
                                int                image_count,
                                const char *const *images,
                                const int         *rank_task_counts,
                                const char         rank_hosts[][64]) {
  struct tm *tm_start = localtime(&started_time);
  char       started_str[64];
  strftime(started_str, sizeof(started_str), "%Y-%m-%d %H:%M:%S", tm_start);

  struct tm *tm_finished = localtime(&finished_time);
  char       finished_str[64];
  strftime(
    finished_str, sizeof(finished_str), "%Y-%m-%d %H:%M:%S", tm_finished);

  char log_path[PATH_MAX];
  strftime(
    log_path, sizeof(log_path), "logs/run_%Y%m%d_%H%M%S.log", tm_finished);

  // Asegurar que el directorio logs exista
  mkdir("logs", 0777);

  FILE *log_file = fopen(log_path, "w");
  if (!log_file) {
    fprintf(
      stderr, "Error: no se pudo crear el archivo de log '%s'\n", log_path);
    return;
  }

  // Obtener rutas absolutas para el machinefile
  char abs_machinefile[PATH_MAX] = "";
  char cwd[PATH_MAX];
  if (getcwd(cwd, sizeof(cwd)) != NULL) {
    snprintf(abs_machinefile, sizeof(abs_machinefile), "%s/machinefile", cwd);
  } else {
    strncpy(abs_machinefile, "machinefile", sizeof(abs_machinefile) - 1);
  }

  // Parsear machinefile para obtener los hosts activos y sus slots
  active_hosts_count = 0;
  parse_machinefile("machinefile");

  // Mapeo hardcodeado de hostnames de SO a alias de machinefile
  struct {
    const char *os_name;
    const char *alias;
  } host_map[] = {{"omarchyDesktop", "ub0"},
                  {"Thinkpad", "ub1"},
                  {"cruzazul", "ub2"},
                  {"manuel-ASUS", "ub3"},
                  {"javierc", "ub4"},
                  {"alejandrogs", "ub5"}};
  int map_size = sizeof(host_map) / sizeof(host_map[0]);

  // Resumen de tareas por computadora (host)
  typedef struct {
    char host[64];
    int  task_count;
    int  slots;
  } host_task_summary;

  host_task_summary summaries[128];
  int               summary_count = 0;
  memset(summaries, 0, sizeof(summaries));

  // Inicializar summaries con los hosts del machinefile
  for (int ah = 0; ah < active_hosts_count && summary_count < 128; ah++) {
    strncpy(summaries[summary_count].host,
            active_hosts[ah].host,
            sizeof(summaries[summary_count].host) - 1);
    summaries[summary_count].slots      = active_hosts[ah].slots;
    summaries[summary_count].task_count = 0;
    summary_count++;
  }

  // Acumular tareas de los ranks
  for (int r = 0; r < world_size; r++) {
    if (rank_task_counts[r] > 0 || (world_size == 1 && r == 0)) {
      const char *hname = rank_hosts[r];
      if (strlen(hname) == 0) {
        hname = "localhost";
      }

      for (int m = 0; m < map_size; m++) {
        if (strcmp(hname, host_map[m].os_name) == 0) {
          hname = host_map[m].alias;
          break;
        }
      }

      int found = -1;
      for (int s = 0; s < summary_count; s++) {
        if (strcmp(summaries[s].host, hname) == 0) {
          found = s;
          break;
        }
      }
      if (found != -1) {
        summaries[found].task_count += rank_task_counts[r];
      } else if (summary_count < 128) {
        strncpy(summaries[summary_count].host,
                hname,
                sizeof(summaries[summary_count].host) - 1);
        summaries[summary_count].task_count = rank_task_counts[r];
        summaries[summary_count].slots      = 1;
        summary_count++;
      }
    }
  }

  fprintf(log_file, "MPI image processing log\n");
  fprintf(log_file, "Started: %s\n", started_str);
  fprintf(log_file, "Finished: %s\n", finished_str);
  fprintf(log_file, "Elapsed: %.4f s\n", elapsed);
  fprintf(log_file, "Source machinefile: %s\n", abs_machinefile);
  fprintf(log_file, "Active machinefile: %s\n", abs_machinefile);

  if (summary_count > 0) {
    fprintf(log_file, "Active hosts: %d\n", summary_count);
    for (int j = 0; j < summary_count; j++) {
      fprintf(log_file,
              "  - %s slots=%d (tasks=%d)\n",
              summaries[j].host,
              summaries[j].slots,
              summaries[j].task_count);
    }
  } else {
    fprintf(log_file, "Active hosts: 1\n");
    fprintf(log_file, "  - ub0 slots=%d (tasks=%d)\n", world_size, task_count);
  }

  // Transformaciones
  fprintf(log_file, "Transforms: ");
  char transforms_str[256] = "";
  if (do_vg)
    strcat(transforms_str, "vg, ");
  if (do_vc)
    strcat(transforms_str, "vc, ");
  if (do_hg)
    strcat(transforms_str, "hg, ");
  if (do_hc)
    strcat(transforms_str, "hc, ");
  if (do_dg)
    strcat(transforms_str, "dg, ");
  if (do_dc)
    strcat(transforms_str, "dc, ");
  int t_len = strlen(transforms_str);
  if (t_len > 2) {
    transforms_str[t_len - 2] = '\0';
  }
  fprintf(log_file, "%s\n", transforms_str);

  fprintf(log_file, "Kernel dg: %d\n", kernel_dg);
  fprintf(log_file, "Kernel dc: %d\n", kernel_dc);
  fprintf(log_file, "Return code: 0\n");

  fprintf(log_file, "\n-- Task distribution --\n");
  fprintf(log_file, "Tasks per rank:\n");
  for (int r = 0; r < world_size; r++) {
    const char *hname = rank_hosts[r];
    if (strlen(hname) == 0) {
      hname = "localhost";
    }

    for (int m = 0; m < map_size; m++) {
      if (strcmp(hname, host_map[m].os_name) == 0) {
        hname = host_map[m].alias;
        break;
      }
    }

    fprintf(log_file,
            "  - Rank %d: %d tasks (host: %s)\n",
            r,
            rank_task_counts[r],
            hname);
  }

  fprintf(log_file, "\n-- Task summary --\n");
  int   summary_emitted = 0;
  char *stdout_copy     = malloc(strlen(stdout_buffer) + 1);
  if (stdout_copy) {
    strcpy(stdout_copy, stdout_buffer);
    char *line = strtok(stdout_copy, "\n");
    while (line != NULL) {
      if (strncmp(line, "TASK:", 5) == 0 || strncmp(line, "SUMMARY:", 8) == 0) {
        fprintf(log_file, "%s\n", line);
        summary_emitted = 1;
      }
      line = strtok(NULL, "\n");
    }
    free(stdout_copy);
  }
  if (!summary_emitted) {
    fprintf(log_file, "No task summary lines were emitted.\n");
  }

  fprintf(log_file, "\n-- Stdout --\n");
  fprintf(log_file, "%s", stdout_buffer);

  fprintf(log_file, "\n\n-- Stderr --\n");
  fprintf(log_file, "%s", stderr_buffer);

  fclose(log_file);

  // Imprimir LOG_FILE para que gui.py lo capture
  printf("LOG_FILE:%s\n", log_path);
  fflush(stdout);
}


typedef enum {
  TASK_VG,
  TASK_VC,
  TASK_HG,
  TASK_HC,
  TASK_DG,
  TASK_DC,
} task_kind;

typedef struct {
  int       image_index;
  task_kind kind;
  int       kernel_size;
} image_task;

typedef struct {
  char path[512];
} image_path_entry;

static const char *task_kind_name(task_kind kind) {
  switch (kind) {
    case TASK_VG:
      return "vg";
    case TASK_VC:
      return "vc";
    case TASK_HG:
      return "hg";
    case TASK_HC:
      return "hc";
    case TASK_DG:
      return "dg";
    case TASK_DC:
      return "dc";
  }
  return "unknown";
}

static const char *task_suffix(task_kind kind) {
  return task_kind_name(kind);
}

/* ─── carga el encabezado BMP de un archivo ─── */
static int load_bmp_info(const char *path, bmp_image_info *bmp) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "Error: no se pudo abrir '%s'\n", path);
    return 0;
  }
  if (!bmp_read_info(f, bmp)) {
    fprintf(stderr, "Error: encabezado BMP inválido en '%s'\n", path);
    fclose(f);
    return 0;
  }
  fclose(f);
  return 1;
}

static int compare_image_paths(const void *lhs, const void *rhs) {
  const image_path_entry *left  = (const image_path_entry *)lhs;
  const image_path_entry *right = (const image_path_entry *)rhs;
  return strcmp(left->path, right->path);
}

static int load_default_images(const char       *directory,
                               image_path_entry *storage,
                               const char      **images,
                               int               max_images) {
  DIR *dir = opendir(directory);
  if (!dir) {
    return 0;
  }

  image_path_entry entries[MAX_IMAGES];
  int              entry_count = 0;
  struct dirent   *entry;

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.') {
      continue;
    }

    const char *dot = strrchr(entry->d_name, '.');
    if (!dot || strcmp(dot, ".bmp") != 0) {
      continue;
    }

    if (entry_count >= max_images) {
      break;
    }

    snprintf(entries[entry_count].path,
             sizeof(entries[entry_count].path),
             "%s/%s",
             directory,
             entry->d_name);
    entry_count++;
  }

  closedir(dir);

  if (entry_count == 0) {
    return 0;
  }

  qsort(entries, (size_t)entry_count, sizeof(entries[0]), compare_image_paths);

  for (int idx = 0; idx < entry_count && idx < max_images; idx++) {
    storage[idx] = entries[idx];
    images[idx]  = storage[idx].path;
  }

  return entry_count < max_images ? entry_count : max_images;
}

static void abort_with_usage(int rank, const char *message) {
  if (rank == 0 && message) {
    fprintf(stderr, "%s\n", message);
  }
  MPI_Abort(MPI_COMM_WORLD, 1);
}

static void run_task(const image_task  *task,
                     const char *const *images,
                     bmp_image_info    *bmps) {
  if (!task || !images || !bmps) {
    return;
  }

  const char     *input_path = images[task->image_index];
  bmp_image_info *bmp        = &bmps[task->image_index];

  switch (task->kind) {
    case TASK_VG:
      inv_vt_gris(input_path, "vg", bmp);
      break;
    case TASK_VC:
      inv_vt_color(input_path, "vc", bmp);
      break;
    case TASK_HG:
      inv_hz_gris(input_path, "hg", bmp);
      break;
    case TASK_HC:
      inv_hz_color(input_path, "hc", bmp);
      break;
    case TASK_DG:
      desenfoque_gris(input_path, "dg", task->kernel_size, bmp);
      break;
    case TASK_DC:
      desenfoque(input_path, "dc", task->kernel_size, bmp);
      break;
  }
}

int main(int argc, char *argv[]) {
  time_t started_time = time(NULL);
  int    world_size   = 1;
  int    world_rank   = 0;

  MPI_Init(&argc, &argv);
  double total_start = MPI_Wtime();
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

  int *rank_task_counts  = calloc(world_size, sizeof(int));
  char (*rank_hosts)[64] = calloc(world_size, 64);

  const char      *images[MAX_IMAGES];
  image_path_entry default_image_storage[MAX_IMAGES];
  int              image_count         = 0;
  int              requested_processes = 0;

  /* ─── Parseo de argumentos ─── */
  int i = 1;
  while (i < argc) {

    if (strcmp(argv[i], "--transforms") == 0) {
      i++;
      /* consumir todos los códigos hasta el siguiente flag o fin */
      while (i < argc && argv[i][0] != '-') {
        if (strcmp(argv[i], "vg") == 0)
          do_vg = 1;
        else if (strcmp(argv[i], "vc") == 0)
          do_vc = 1;
        else if (strcmp(argv[i], "hg") == 0)
          do_hg = 1;
        else if (strcmp(argv[i], "hc") == 0)
          do_hc = 1;
        else if (strcmp(argv[i], "dg") == 0)
          do_dg = 1;
        else if (strcmp(argv[i], "dc") == 0)
          do_dc = 1;
        i++;
      }

    } else if (strcmp(argv[i], "--kernel-dg") == 0) {
      i++;
      if (i < argc) {
        kernel_dg = atoi(argv[i]);
        i++;
      }

    } else if (strcmp(argv[i], "--kernel-dc") == 0) {
      i++;
      if (i < argc) {
        kernel_dc = atoi(argv[i]);
        i++;
      }

    } else if (strcmp(argv[i], "--threads") == 0) {
      i++;
      if (i < argc) {
        requested_processes = atoi(argv[i]);
        i++;
      }

    } else if (argv[i][0] != '-' && image_count < MAX_IMAGES) {
      images[image_count++] = argv[i++];

    } else {
      i++;
    }
  }

  if (image_count == 0) {
    image_count = load_default_images(
      "img_to_process", default_image_storage, images, MAX_IMAGES);
  }

  /* ─── Validaciones básicas ─── */
  if (image_count == 0) {
    abort_with_usage(world_rank,
                     "Error: no se proporcionaron imágenes.\n"
                     "Uso: mpirun -np <procesos> ./imgprocP img1.bmp [img2...]"
                     " --transforms vg vc ...\n"
                     "O coloque archivos .bmp en img_to_process/.");
  }
  if (!do_vg && !do_vc && !do_hg && !do_hc && !do_dg && !do_dc) {
    abort_with_usage(world_rank,
                     "Error: no se seleccionó ninguna transformación.");
  }

  /* ─── Leer encabezados BMP ─── */
  bmp_image_info bmps[MAX_IMAGES];
  memset(bmps, 0, sizeof(bmps));

  for (int img = 0; img < image_count; img++) {
    if (!load_bmp_info(images[img], &bmps[img])) {
      for (int j = 0; j < img; j++)
        bmp_free_info(&bmps[j]);
      abort_with_usage(world_rank, "Error: no se pudo leer un encabezado BMP.");
    }
  }

  /* ─── Construir lista plana de tareas: una transformación por imagen ─── */
  static image_task tasks[MAX_TASKS];
  int               task_count = 0;
  for (int img = 0; img < image_count; img++) {
    if (do_vg)
      tasks[task_count++] = (image_task){img, TASK_VG, 0};
    if (do_vc)
      tasks[task_count++] = (image_task){img, TASK_VC, 0};
    if (do_hg)
      tasks[task_count++] = (image_task){img, TASK_HG, 0};
    if (do_hc)
      tasks[task_count++] = (image_task){img, TASK_HC, 0};
    if (do_dg)
      tasks[task_count++] = (image_task){img, TASK_DG, kernel_dg};
    if (do_dc)
      tasks[task_count++] = (image_task){img, TASK_DC, kernel_dc};
  }

  /* ─── Distribución dinámica maestro/trabajador ───
     El rank 0 reparte tareas bajo demanda: cada trabajador pide una tarea,
     la ejecuta y vuelve a pedir. Los ranks rápidos (ub0) completan más
     tareas que los lentos (ub2) de forma automática, sin pesos fijos.

     Protocolo de mensajes:
       TAG_REQUEST  trabajador -> maestro : "estoy libre"
       TAG_WORK     maestro -> trabajador : payload de 3 enteros (tarea)
       TAG_STOP     maestro -> trabajador : "no queda trabajo, termina"

     El payload viaja como int[3] (image_index, kind, kernel_size) para ser
     seguro entre arquitecturas (x86_64 <-> aarch64). No se envían píxeles:
     cada rank ya cargó images[] y bmps[] localmente. */
  enum { TAG_REQUEST = 1, TAG_WORK = 2, TAG_STOP = 3 };

  MPI_Barrier(MPI_COMM_WORLD);
  const double start = MPI_Wtime();

  if (world_size == 1) {
    /* sin trabajadores: el único proceso ejecuta todo */
    char hostname[64] = "localhost";
    char processor_name[MPI_MAX_PROCESSOR_NAME];
    int  processor_len = 0;
    MPI_Get_processor_name(processor_name, &processor_len);
    if (processor_len > 0) {
      if (processor_len >= 64)
        processor_name[63] = '\0';
      else
        processor_name[processor_len] = '\0';
      strcpy(hostname, processor_name);
    }

    rank_task_counts[0] = task_count;
    strncpy(rank_hosts[0], hostname, 63);
    rank_hosts[0][63] = '\0';

    for (int t = 0; t < task_count; t++) {
      image_task *task       = &tasks[t];
      const char *input_path = images[task->image_index];
      char        output_name[256];
      char        output_path[512];
      bmp_build_output_name(
        input_path, task_suffix(task->kind), output_name, sizeof(output_name));
      snprintf(output_path,
               sizeof(output_path),
               "%s/%s.bmp",
               BMP_OUTPUT_DIR,
               output_name);

      const double task_start = MPI_Wtime();
      run_task(task, images, bmps);
      const double task_elapsed = MPI_Wtime() - task_start;

      log_printf("TASK:id=%d rank=0 host=%s image=%s transform=%s kernel=%d "
                 "output=%s elapsed=%.4f\n",
                 t + 1,
                 hostname,
                 input_path,
                 task_kind_name(task->kind),
                 task->kernel_size,
                 output_path,
                 task_elapsed);
    }

  } else if (world_rank == 0) {
    /* ─── maestro: despacha tareas bajo demanda ─── */
    int           next   = 0;
    int           active = world_size - 1;
    int           msg[4];
    MPI_Status    st;
    worker_report report;

    while (active > 0) {
      MPI_Recv(&report,
               sizeof(worker_report),
               MPI_BYTE,
               MPI_ANY_SOURCE,
               TAG_REQUEST,
               MPI_COMM_WORLD,
               &st);

      if (report.completed_task_id > 0) {
        if (report.rank >= 0 && report.rank < world_size) {
          rank_task_counts[report.rank]++;
          strncpy(rank_hosts[report.rank], report.host, 63);
          rank_hosts[report.rank][63] = '\0';
        }
        int task_idx = report.completed_task_id - 1;
        if (task_idx >= 0 && task_idx < task_count) {
          image_task *task       = &tasks[task_idx];
          const char *input_path = images[task->image_index];
          char        output_name[256];
          char        output_path[512];
          bmp_build_output_name(input_path,
                                task_suffix(task->kind),
                                output_name,
                                sizeof(output_name));
          snprintf(output_path,
                   sizeof(output_path),
                   "%s/%s.bmp",
                   BMP_OUTPUT_DIR,
                   output_name);

          log_printf("TASK:id=%d rank=%d host=%s image=%s transform=%s "
                     "kernel=%d output=%s elapsed=%.4f\n",
                     report.completed_task_id,
                     report.rank,
                     report.host,
                     input_path,
                     task_kind_name(task->kind),
                     task->kernel_size,
                     output_path,
                     report.elapsed);
        }
      }

      if (next < task_count) {
        msg[0] = next + 1;
        msg[1] = tasks[next].image_index;
        msg[2] = (int)tasks[next].kind;
        msg[3] = tasks[next].kernel_size;
        MPI_Send(msg, 4, MPI_INT, st.MPI_SOURCE, TAG_WORK, MPI_COMM_WORLD);
        next++;
      } else {
        msg[0] = 0;
        msg[1] = 0;
        msg[2] = 0;
        msg[3] = 0;
        MPI_Send(msg, 4, MPI_INT, st.MPI_SOURCE, TAG_STOP, MPI_COMM_WORLD);
        active--;
      }
    }

  } else {
    /* ─── trabajador: pide, ejecuta, repite hasta TAG_STOP ─── */
    int        msg[4];
    char       processor_name[MPI_MAX_PROCESSOR_NAME];
    int        processor_len = 0;
    MPI_Status st;

    MPI_Get_processor_name(processor_name, &processor_len);
    if (processor_len >= MPI_MAX_PROCESSOR_NAME) {
      processor_name[MPI_MAX_PROCESSOR_NAME - 1] = '\0';
    } else {
      processor_name[processor_len] = '\0';
    }

    worker_report report;
    report.rank              = world_rank;
    report.completed_task_id = 0;
    report.elapsed           = 0.0;
    strncpy(report.host, processor_name, sizeof(report.host) - 1);
    report.host[sizeof(report.host) - 1] = '\0';

    for (;;) {
      MPI_Send(&report,
               sizeof(worker_report),
               MPI_BYTE,
               0,
               TAG_REQUEST,
               MPI_COMM_WORLD);
      MPI_Recv(msg, 4, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &st);

      if (st.MPI_TAG == TAG_STOP)
        break;

      image_task   task       = {msg[1], (task_kind)msg[2], msg[3]};
      const double task_start = MPI_Wtime();
      run_task(&task, images, bmps);
      const double task_elapsed = MPI_Wtime() - task_start;

      report.completed_task_id = msg[0];
      report.elapsed           = task_elapsed;
    }
  }

  const double local_elapsed = MPI_Wtime() - start;
  double       max_elapsed   = 0.0;
  MPI_Reduce(
    &local_elapsed, &max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

  /* ─── Liberar memoria ─── */
  for (int img = 0; img < image_count; img++)
    bmp_free_info(&bmps[img]);

  /* ─── Salida que la GUI parsea ─── */
  if (world_rank == 0) {
    if (requested_processes > 0 && requested_processes != world_size) {
      log_stderr_printf(
        "Aviso: --threads se ignora con MPI; use mpirun -np %d.\n", world_size);
    }
    log_printf("SUMMARY:tasks=%d ranks=%d images=%d\n",
               task_count,
               world_size,
               image_count);
    log_printf("TIEMPO:%.4f\n", max_elapsed);
    log_printf("THREADS:%d\n", world_size);

    time_t finished_time = time(NULL);
    double total_end     = MPI_Wtime();
    write_execution_log(started_time,
                        finished_time,
                        total_end - total_start,
                        world_size,
                        task_count,
                        image_count,
                        images,
                        rank_task_counts,
                        (const char (*)[64])rank_hosts);
  }

  free(rank_task_counts);
  free(rank_hosts);

  MPI_Finalize();

  return 0;
}