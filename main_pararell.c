#include "bmp_utils.h"
#include "desenfoque.h"
#include "gris.h"
#include "inv_hz.h"
#include "inv_vt.h"
#include <omp.h>
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

void process_image_parallel(const char *input_file, int thread_count) {
  FILE *image = fopen(input_file, "rb");

  if (!image) {
    LOG("Error: No se pudo abrir la imagen original.\n");
    return;
  }

  bmp_image_info bmp;
  if (!bmp_read_info(image, &bmp)) {
    LOG("Error: encabezado BMP invalido o incompleto.\n");
    fclose(image);
    return;
  }
  fclose(image);

  LOG("Processing image: %s\n", input_file);

  // Clean the input file name
  const char *base_name = input_file;
  const char *slash     = strrchr(input_file, '/');
  if (slash) {
    base_name = slash + 1;
  }

  size_t      base_len = strlen(base_name);
  const char *dot      = strrchr(base_name, '.');
  if (dot && dot != base_name) {
    base_len = (size_t)(dot - base_name);
  }

  char input_base[128];
  snprintf(input_base, sizeof(input_base), "%.*s", (int)base_len, base_name);

#pragma omp parallel sections num_threads(thread_count) default(none)          \
  shared(input_file, input_base, bmp)
  {
#pragma omp section
    {
      char output_name[128];
      snprintf(output_name, sizeof(output_name), "%s_gris", input_base);
      LOG("Applying grayscale filter...\n");
      gris(input_file, output_name, &bmp);
    }

#pragma omp section
    {
      char output_name[128];
      snprintf(output_name, sizeof(output_name), "%s_desenfoque", input_base);
      LOG("Applying blur filter...\n");
      desenfoque(input_file, output_name, 16, &bmp);
    }

#pragma omp section
    {
      char output_name[128];
      snprintf(
        output_name, sizeof(output_name), "%s_desenfoque_gris", input_base);
      LOG("Applying gray blur filter...\n");
      desenfoque_gris(input_file, output_name, 16, &bmp);
    }

#pragma omp section
    {
      char output_name[128];
      snprintf(output_name, sizeof(output_name), "%s_inverted", input_base);
      LOG("Applying inverting image...\n");
      inv_hz_color(input_file, output_name, &bmp);
    }

#pragma omp section
    {
      char output_name[128];
      snprintf(
        output_name, sizeof(output_name), "%s_inverted_gris", input_base);
      LOG("Applying inverting image gray...\n");
      inv_hz_gris(input_file, output_name, &bmp);
    }

#pragma omp section
    {
      char output_name[128];
      snprintf(output_name, sizeof(output_name), "%s_inverted_vt", input_base);
      LOG("Applying inverting vertically image...\n");
      inv_vt_color(input_file, output_name, &bmp);
    }

#pragma omp section
    {
      char output_name[128];
      snprintf(
        output_name, sizeof(output_name), "%s_inverted_gris_vt", input_base);
      LOG("Applying inverting vertically image gray...\n");
      inv_vt_gris(input_file, output_name, &bmp);
    }
  }

  bmp_free_info(&bmp);

  LOG("Processing complete!\n");
  LOG("Output files created in ./img/ using input base name + suffix.\n");
}

// int main() {
//   int thread_count = 18;

//   process_image_parallel("sample1.bmp", thread_count);

//   process_image_parallel("sample2.bmp", thread_count);

//   process_image_parallel("sample3.bmp", thread_count);

//   return 0;
// }


int main() {
  // Enable nested parallelism so the threads in main can spawn their own
  // threads Note: Depending on your OpenMP version, you might use
  omp_set_max_active_levels(2);
  // omp_set_nested(1);

  // Distribute threads: 6 threads per image x 3 images = 18 active threads
  // max
  int inner_thread_count = 6;

// Wrap the sections in a parallel block.
// We specify num_threads(3) because there are 3 sections (files) to process.
#pragma omp parallel sections num_threads(3) default(none)                     \
  shared(inner_thread_count)
  {
#pragma omp section
    {
      process_image_parallel("sample1.bmp", inner_thread_count);
    }

#pragma omp section
    {
      process_image_parallel("sample2.bmp", inner_thread_count);
    }

#pragma omp section
    {
      process_image_parallel("sample3.bmp", inner_thread_count);
    }
  }

  return 0;
}