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
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_IMAGES 50
#define MAX_TASKS (MAX_IMAGES * 6)

/* ─── flags de transformación (globales de solo lectura en runtime) ─── */
static int do_vg = 0, do_vc = 0, do_hg = 0, do_hc = 0, do_dg = 0, do_dc = 0;
static int kernel_dg = 16, kernel_dc = 16;

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
    images[idx] = storage[idx].path;
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
  int world_size = 1;
  int world_rank = 0;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

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

  /* Distribución por imagen: cada rank toma imágenes completas
     (img = world_rank; img < image_count; img += world_size) y
     ejecuta todas las transformaciones seleccionadas sobre esa imagen. */

  MPI_Barrier(MPI_COMM_WORLD);
  const double start = MPI_Wtime();

  for (int img = world_rank; img < image_count; img += world_size) {
    if (do_vg) {
      run_task(&(image_task){img, TASK_VG, 0}, images, bmps);
    }
    if (do_vc) {
      run_task(&(image_task){img, TASK_VC, 0}, images, bmps);
    }
    if (do_hg) {
      run_task(&(image_task){img, TASK_HG, 0}, images, bmps);
    }
    if (do_hc) {
      run_task(&(image_task){img, TASK_HC, 0}, images, bmps);
    }
    if (do_dg) {
      run_task(&(image_task){img, TASK_DG, kernel_dg}, images, bmps);
    }
    if (do_dc) {
      run_task(&(image_task){img, TASK_DC, kernel_dc}, images, bmps);
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
      fprintf(stderr,
              "Aviso: --threads se ignora con MPI; use mpirun -np %d.\n",
              world_size);
    }
    printf("TIEMPO:%.4f\n", max_elapsed);
    printf("THREADS:%d\n", world_size);
  }

  MPI_Finalize();

  return 0;
}