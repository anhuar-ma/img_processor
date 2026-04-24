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

static int load_bmp_info(const char *input_file, bmp_image_info *bmp) {
  FILE *image = fopen(input_file, "rb");
  if (!image) {
    LOG("Error: No se pudo abrir la imagen original.\n");
    return 0;
  }

  if (!bmp_read_info(image, bmp)) {
    LOG("Error: encabezado BMP invalido o incompleto.\n");
    fclose(image);
    return 0;
  }

  fclose(image);
  return 1;
}


int main() {


  // Habilitar el paralelismo anidado para que los hilos de main puedan crear
  // sus propios hilos. Nota: según tu versión de OpenMP, podrías usar
  // omp_set_nested(1);

  const int thread_count = 2;


  const char *img1 = "sample1.bmp";
  const char *img2 = "sample2.bmp";
  const char *img3 = "sample3.bmp";

  bmp_image_info bmp1;
  bmp_image_info bmp2;
  bmp_image_info bmp3;

  if (!load_bmp_info(img1, &bmp1) || !load_bmp_info(img2, &bmp2)
      || !load_bmp_info(img3, &bmp3)) {
    bmp_free_info(&bmp1);
    bmp_free_info(&bmp2);
    bmp_free_info(&bmp3);
    return 1;
  }

  omp_set_num_threads(thread_count);
  const double START = omp_get_wtime();


#pragma omp parallel sections
  {
    // Funcion que hace solamente gris la imagen
    // #pragma omp section
    //     {
    //       char output_name[128];
    //       snprintf(output_name, sizeof(output_name), "%s_gris", input_base);
    //       LOG("Applying grayscale filter...\n");
    //       gris(input_file, output_name, &bmp);
    //     }

    // --------IMG1----------------
#pragma omp section
    {
      LOG("Applying blur filter...\n");
      desenfoque(img1, "desenfoque", 16, &bmp1);
    }

#pragma omp section
    {
      LOG("Applying gray blur filter...\n");
      desenfoque_gris(img1, "desenfoque_gris", 16, &bmp1);
    }

#pragma omp section
    {
      LOG("Applying inverting image...\n");
      inv_hz_color(img1, "inverted", &bmp1);
    }

#pragma omp section
    {
      LOG("Applying inverting image gray...\n");
      inv_hz_gris(img1, "inverted_gris", &bmp1);
    }

#pragma omp section
    {
      LOG("Applying inverting vertically image...\n");
      inv_vt_color(img1, "inverted_vt", &bmp1);
    }

#pragma omp section
    {
      LOG("Applying inverting vertically image gray...\n");
      inv_vt_gris(img1, "inverted_gris_vt", &bmp1);
    }


    // --------IMG2----------------

#pragma omp section
    {
      LOG("Applying blur filter...\n");
      desenfoque(img2, "desenfoque", 16, &bmp2);
    }

#pragma omp section
    {
      LOG("Applying gray blur filter...\n");
      desenfoque_gris(img2, "desenfoque_gris", 16, &bmp2);
    }

#pragma omp section
    {
      LOG("Applying inverting image...\n");
      inv_hz_color(img2, "inverted", &bmp2);
    }

#pragma omp section
    {
      LOG("Applying inverting image gray...\n");
      inv_hz_gris(img2, "inverted_gris", &bmp2);
    }

#pragma omp section
    {
      LOG("Applying inverting vertically image...\n");
      inv_vt_color(img2, "inverted_vt", &bmp2);
    }

#pragma omp section
    {
      LOG("Applying inverting vertically image gray...\n");
      inv_vt_gris(img2, "inverted_gris_vt", &bmp2);
    }


    // --------IMG3----------------

#pragma omp section
    {
      LOG("Applying blur filter...\n");
      desenfoque(img3, "desenfoque", 16, &bmp3);
    }

#pragma omp section
    {
      LOG("Applying gray blur filter...\n");
      desenfoque_gris(img3, "desenfoque_gris", 16, &bmp3);
    }

#pragma omp section
    {
      LOG("Applying inverting image...\n");
      inv_hz_color(img3, "inverted", &bmp3);
    }

#pragma omp section
    {
      LOG("Applying inverting image gray...\n");
      inv_hz_gris(img3, "inverted_gris", &bmp3);
    }

#pragma omp section
    {
      LOG("Applying inverting vertically image...\n");
      inv_vt_color(img3, "inverted_vt", &bmp3);
    }

#pragma omp section
    {
      LOG("Applying inverting vertically image gray...\n");
      inv_vt_gris(img3, "inverted_gris_vt", &bmp3);
    }
  }


  bmp_free_info(&bmp1);
  bmp_free_info(&bmp2);
  bmp_free_info(&bmp3);

  LOG("Processing complete!\n");
  LOG("Output files created in ./img/ using input base name + suffix.\n");


  const double STOP = omp_get_wtime();

  printf("Threads = %d \n", (thread_count));
  printf("Tiempo = %lf \n", (STOP - START));

  return 0;
}