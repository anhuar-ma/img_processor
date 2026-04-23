#include "bmp_utils.h"
#include "desenfoque.h"
#include "gris.h"
#include "inv_hz.h"
#include "inv_vt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #define ENABLE_LOGS
#ifdef ENABLE_LOGS
#  define LOG(...) printf(__VA_ARGS__)
#else
#  define LOG(...)                                                             \
    do {                                                                       \
    } while (0)
#endif

int main() {
  const char *input_file   = "sample.bmp";
  int         thread_count = 16;
  FILE       *image        = fopen(input_file, "rb");

  if (!image) {
    LOG("Error: No se pudo abrir la imagen original.\n");
    return 1;
  }

  bmp_image_info bmp;
  if (!bmp_read_info(image, &bmp)) {
    LOG("Error: encabezado BMP invalido o incompleto.\n");
    fclose(image);
    return 1;
  }
  fclose(image);

  LOG("Processing image: %s\n", input_file);

#pragma omp parallel sections num_threads(thread_count) default(none)          \
  shared(input_file, bmp)
  {
#pragma omp section
    {
      LOG("Applying grayscale filter...\n");
      gris(input_file, "img_gris.bmp", &bmp);
    }

#pragma omp section
    {
      LOG("Applying blur filter...\n");
      desenfoque(input_file, "img_desenfoque", 16, &bmp);
    }

#pragma omp section
    {
      LOG("Applying gray blur filter...\n");
      desenfoque_gris(input_file, "img_desenfoque_gris", 16, &bmp);
    }

#pragma omp section
    {
      LOG("Applying inverting image...\n");
      inv_hz_color(input_file, "img_inverted", &bmp);
    }

#pragma omp section
    {
      LOG("Applying inverting image gray...\n");
      inv_hz_gris(input_file, "img_inverted_gris", &bmp);
    }

#pragma omp section
    {
      LOG("Applying inverting vertically image...\n");
      inv_vt_color(input_file, "img_inverted_vt", &bmp);
    }

#pragma omp section
    {
      LOG("Applying inverting vertically image gray...\n");
      inv_vt_gris(input_file, "img_inverted_gris_vt", &bmp);
    }
  }

  bmp_free_info(&bmp);

  LOG("Processing complete!\n");
  LOG("Output files created:\n");
  LOG("  - img_gris.bmp\n");
  LOG("  - ./img/img_desenfoque.bmp\n");

  return 0;
}
