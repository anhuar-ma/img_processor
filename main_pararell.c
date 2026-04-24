#include "bmp_utils.h"
#include "desenfoque.h"
#include "gris.h"
#include "inv_hz.h"
#include "inv_vt.h"
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>

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

#pragma omp parallel sections num_threads(thread_count) default(none)          \
  shared(input_file, bmp)
  {
    // Funcion que hace solamente gris la imagen
    // #pragma omp section
    //     {
    //       char output_name[128];
    //       snprintf(output_name, sizeof(output_name), "%s_gris", input_base);
    //       LOG("Applying grayscale filter...\n");
    //       gris(input_file, output_name, &bmp);
    //     }

#pragma omp section
    {
      LOG("Applying blur filter...\n");
      desenfoque(input_file, "desenfoque", 16, &bmp);
    }

#pragma omp section
    {
      LOG("Applying gray blur filter...\n");
      desenfoque_gris(input_file, "desenfoque_gris", 16, &bmp);
    }

#pragma omp section
    {
      LOG("Applying inverting image...\n");
      inv_hz_color(input_file, "inverted", &bmp);
    }

#pragma omp section
    {
      LOG("Applying inverting image gray...\n");
      inv_hz_gris(input_file, "inverted_gris", &bmp);
    }

#pragma omp section
    {
      LOG("Applying inverting vertically image...\n");
      inv_vt_color(input_file, "inverted_vt", &bmp);
    }

#pragma omp section
    {
      LOG("Applying inverting vertically image gray...\n");
      inv_vt_gris(input_file, "inverted_gris_vt", &bmp);
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

  const double START = omp_get_wtime();

  // Habilitar el paralelismo anidado para que los hilos de main puedan crear
  // sus propios hilos. Nota: según tu versión de OpenMP, podrías usar
  omp_set_max_active_levels(2);
  // omp_set_nested(1);

  // Distribuir hilos: 2 hilos por imagen x 3 imágenes = 6 hilos activos
  int inner_thread_count = 6;

// Envolver las secciones en un bloque paralelo.
// Especificamos num_threads(3) porque hay 3 secciones (archivos) para
// procesar.
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

  const double STOP = omp_get_wtime();

  printf("Threads = %d \n", (inner_thread_count * 3));
  printf("Tiempo = %lf \n", (STOP - START));

  return 0;
}