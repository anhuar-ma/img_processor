#ifndef INV_VT_H
#define INV_VT_H

#include "bmp_utils.h"
#include <stdio.h>
#include <stdlib.h>


static inline void inv_vt_gris(const char           *input_path,
                               const char           *name_output,
                               const bmp_image_info *bmp) {
  bmp_process_io io;
  if (!bmp_open_process_io(input_path, name_output, bmp, &io)) {
    return;
  }

  FILE *image       = io.image;
  FILE *outputImage = io.outputImage;

  int width           = bmp->width;
  int abs_height      = bmp->abs_height;
  int bytes_per_pixel = bmp->bytes_per_pixel;
  int row_padded      = bmp->row_padded;
  int pixel_bytes     = width * bytes_per_pixel;

  if (bytes_per_pixel < 3) {
    printf("Error: inv_vt_gris requiere BMP de al menos 24 bits.\n");
    fclose(image);
    fclose(outputImage);
    return;
  }

  unsigned char  *output_row = (unsigned char *)malloc((size_t)row_padded);
  unsigned char **rows =
    (unsigned char **)malloc((size_t)abs_height * sizeof(unsigned char *));

  if (!rows || !output_row) {
    printf("Error: no se pudo reservar memoria para procesar filas.\n");
    free(rows);
    free(output_row);
    bmp_close_process_io(&io);
    return;
  }

  for (int i = 0; i < abs_height; i++) {
    rows[i] = (unsigned char *)malloc((size_t)row_padded);
    if (!rows[i]) {
      printf("Error: no se pudo reservar memoria para una fila de pixeles.\n");
      for (int j = 0; j < i; j++) {
        free(rows[j]);
      }
      free(rows);
      free(output_row);
      bmp_close_process_io(&io);
      return;
    }

    if (fread(rows[i], sizeof(unsigned char), (size_t)row_padded, image)
        != (size_t)row_padded) {
      printf("Error: no se pudo leer una fila completa de pixeles.\n");
      for (int j = 0; j <= i; j++) {
        free(rows[j]);
      }
      free(rows);
      free(output_row);
      bmp_close_process_io(&io);
      return;
    }
  }

  for (int y = 0; y < abs_height; y++) {
    unsigned char *input_row = rows[abs_height - 1 - y];

    for (int x = 0; x < width; x++) {
      int src = x * bytes_per_pixel;
      int dst = x * bytes_per_pixel;

      unsigned char b = input_row[src + 0];
      unsigned char g = input_row[src + 1];
      unsigned char r = input_row[src + 2];

      unsigned char gray  = (unsigned char)((21 * r + 72 * g + 7 * b) / 100);
      output_row[dst + 0] = gray;
      output_row[dst + 1] = gray;
      output_row[dst + 2] = gray;

      if (bytes_per_pixel == 4) {
        output_row[dst + 3] = input_row[src + 3];
      }
    }

    for (int p = pixel_bytes; p < row_padded; p++) {
      output_row[p] = input_row[p];
    }

    if (fwrite(
          output_row, sizeof(unsigned char), (size_t)row_padded, outputImage)
        != (size_t)row_padded) {
      printf("Error: no se pudo escribir una fila completa en salida.\n");
      for (int i = 0; i < abs_height; i++) {
        free(rows[i]);
      }
      free(rows);
      free(output_row);
      bmp_close_process_io(&io);
      return;
    }
  }

  for (int i = 0; i < abs_height; i++) {
    free(rows[i]);
  }
  free(rows);
  free(output_row);

  // Escritura en archivo de registro
  // FILE *outputLog = fopen("output_log.txt", "a");
  // if (outputLog != NULL) {
  //   fprintf(outputLog, "Función: inv_vt_gris, con %s\n", input_path);
  //   fprintf(outputLog, "Localidades totales leídas: %d\n", width *
  //   abs_height); fprintf(
  //     outputLog, "Localidades totales escritas: %d\n", width * abs_height);
  //   fprintf(outputLog, "-------------------------------------\n");
  //   fclose(outputLog);
  // } else {
  //   fprintf(stderr,
  //           "Error: No se pudo crear o abrir el archivo de registro.\n");
  // }

  bmp_close_process_io(&io);
}

static inline void inv_vt_color(const char           *input_path,
                                const char           *name_output,
                                const bmp_image_info *bmp) {
  bmp_process_io io;
  if (!bmp_open_process_io(input_path, name_output, bmp, &io)) {
    return;
  }

  FILE *image       = io.image;
  FILE *outputImage = io.outputImage;

  int             width           = bmp->width;
  int             abs_height      = bmp->abs_height;
  int             bytes_per_pixel = bmp->bytes_per_pixel;
  int             row_padded      = bmp->row_padded;
  unsigned char **rows =
    (unsigned char **)malloc((size_t)abs_height * sizeof(unsigned char *));

  if (!rows) {
    printf("Error: no se pudo reservar memoria para procesar filas.\n");
    free(rows);
    bmp_close_process_io(&io);
    return;
  }

  for (int i = 0; i < abs_height; i++) {
    rows[i] = (unsigned char *)malloc((size_t)row_padded);
    if (!rows[i]) {
      printf("Error: no se pudo reservar memoria para una fila de pixeles.\n");
      for (int j = 0; j < i; j++) {
        free(rows[j]);
      }
      free(rows);
      bmp_close_process_io(&io);
      return;
    }

    if (fread(rows[i], sizeof(unsigned char), (size_t)row_padded, image)
        != (size_t)row_padded) {
      printf("Error: no se pudo leer una fila completa de pixeles.\n");
      for (int j = 0; j <= i; j++) {
        free(rows[j]);
      }
      free(rows);
      bmp_close_process_io(&io);
      return;
    }
  }

  for (int y = 0; y < abs_height; y++) {
    unsigned char *source_row = rows[abs_height - 1 - y];
    if (fwrite(
          source_row, sizeof(unsigned char), (size_t)row_padded, outputImage)
        != (size_t)row_padded) {
      printf("Error: no se pudo escribir una fila completa en salida.\n");
      for (int i = 0; i < abs_height; i++) {
        free(rows[i]);
      }
      free(rows);
      bmp_close_process_io(&io);
      return;
    }
  }

  for (int i = 0; i < abs_height; i++) {
    free(rows[i]);
  }
  free(rows);

  // Escritura en archivo de registro
  // FILE *outputLog = fopen("output_log.txt", "a");
  // if (outputLog != NULL) {
  //   fprintf(outputLog, "Función: inv_vt_color, con %s\n", input_path);
  //   fprintf(outputLog, "Localidades totales leídas: %d\n", width *
  //   abs_height); fprintf(
  //     outputLog, "Localidades totales escritas: %d\n", width * abs_height);
  //   fprintf(outputLog, "-------------------------------------\n");
  //   fclose(outputLog);
  // } else {
  //   fprintf(stderr,
  //           "Error: No se pudo crear o abrir el archivo de registro.\n");
  // }

  bmp_close_process_io(&io);
}

#endif // !INV_VT_H