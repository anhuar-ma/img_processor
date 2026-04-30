/*
 * main_pararell.c
 * Procesador de imágenes BMP con paralelismo por tareas (OpenMP)
 *
 * Uso:
 *   ./imgprocP <img1.bmp> [img2.bmp ...] \
 *              --transforms <vg|vc|hg|hc|dg|dc> [...] \
 *              [--kernel-dg N] [--kernel-dc N] [--threads N]
 *
 * Acrónimos de transformación:
 *   vg  → inversión vertical escala de grises
 *   vc  → inversión vertical a colores
 *   hg  → inversión horizontal escala de grises
 *   hc  → inversión horizontal a colores
 *   dg  → desenfoque escala de grises
 *   dc  → desenfoque a colores
 */

#include "functions/bmp_utils.h"
#include "functions/desenfoque.h"
#include "functions/gris.h"
#include "functions/inv_hz.h"
#include "functions/inv_vt.h"
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_IMAGES 10

/* ─── flags de transformación (globales de solo lectura en runtime) ─── */
static int do_vg = 0, do_vc = 0, do_hg = 0, do_hc = 0, do_dg = 0, do_dc = 0;
static int kernel_dg = 16, kernel_dc = 16;

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

int main(int argc, char *argv[]) {
  const char *images[MAX_IMAGES];
  int         image_count  = 0;
  int         thread_count = 18;

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
        thread_count = atoi(argv[i]);
        i++;
      }

    } else if (argv[i][0] != '-' && image_count < MAX_IMAGES) {
      images[image_count++] = argv[i++];

    } else {
      i++;
    }
  }

  /* ─── Validaciones básicas ─── */
  if (image_count == 0) {
    fprintf(stderr, "Error: no se proporcionaron imágenes.\n");
    fprintf(stderr,
            "Uso: ./imgprocP img1.bmp [img2...] --transforms vg vc ...\n");
    return 1;
  }
  if (!do_vg && !do_vc && !do_hg && !do_hc && !do_dg && !do_dc) {
    fprintf(stderr, "Error: no se seleccionó ninguna transformación.\n");
    return 1;
  }

  /* ─── Leer encabezados BMP ─── */
  bmp_image_info bmps[MAX_IMAGES];
  memset(bmps, 0, sizeof(bmps));

  for (int img = 0; img < image_count; img++) {
    if (!load_bmp_info(images[img], &bmps[img])) {
      for (int j = 0; j < img; j++)
        bmp_free_info(&bmps[j]);
      return 1;
    }
  }

  /* ─── Paralelismo por tareas ─── */
  omp_set_num_threads(thread_count);
  const double START = omp_get_wtime();

#pragma omp parallel
  {
#pragma omp single
    {
      for (int img = 0; img < image_count; img++) {


        if (do_vg) {
#pragma omp task firstprivate(img)
          inv_vt_gris(images[img], "vg", &bmps[img]);
        }

        if (do_vc) {
#pragma omp task firstprivate(img)
          inv_vt_color(images[img], "vc", &bmps[img]);
        }

        if (do_hg) {
#pragma omp task firstprivate(img)
          inv_hz_gris(images[img], "hg", &bmps[img]);
        }

        if (do_hc) {
#pragma omp task firstprivate(img)
          inv_hz_color(images[img], "hc", &bmps[img]);
        }

        if (do_dg) {
          int k = kernel_dg;
#pragma omp task firstprivate(img, k)
          desenfoque_gris(images[img], "dg", k, &bmps[img]);
        }

        if (do_dc) {
          int k = kernel_dc;
#pragma omp task firstprivate(img, k)
          desenfoque(images[img], "dc", k, &bmps[img]);
        }
      }
    } /* end single */
  } /* end parallel */

  const double STOP = omp_get_wtime();

  /* ─── Liberar memoria ─── */
  for (int img = 0; img < image_count; img++)
    bmp_free_info(&bmps[img]);

  /* ─── Salida que la GUI parsea ─── */
  printf("TIEMPO:%.4f\n", STOP - START);
  printf("THREADS:%d\n", thread_count);

  return 0;
}