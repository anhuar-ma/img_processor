#ifndef DESENFOQUE_H
#define DESENFOQUE_H

#include "bmp_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void desenfoque_gris(const char           *input_path,
                     const char           *output_suffix,
                     int                   kernel_size,
                     const bmp_image_info *bmp) {
  char output_name[256];
  bmp_build_output_name(
    input_path, output_suffix, output_name, sizeof(output_name));

  bmp_process_io io;
  if (!bmp_open_process_io(input_path, output_name, bmp, &io)) {
    return;
  }

  FILE *image       = io.image;
  FILE *outputImage = io.outputImage;

  int width           = bmp->width;
  int abs_height      = bmp->abs_height;
  int bytes_per_pixel = bmp->bytes_per_pixel;
  int row_padded      = bmp->row_padded;

  unsigned char **input_rows =
    (unsigned char **)malloc(abs_height * sizeof(unsigned char *));
  unsigned char **output_rows =
    (unsigned char **)malloc(abs_height * sizeof(unsigned char *));
  unsigned char **temp_rows =
    (unsigned char **)malloc(abs_height * sizeof(unsigned char *));

  for (int i = 0; i < abs_height; i++) {
    input_rows[i]  = (unsigned char *)malloc(row_padded);
    output_rows[i] = (unsigned char *)malloc(row_padded);
    temp_rows[i]   = (unsigned char *)malloc(row_padded);
    if (!input_rows[i] || !output_rows[i] || !temp_rows[i]) {
      printf("Error: no se pudo reservar memoria para las filas.\n");
      for (int j = 0; j <= i; j++) {
        free(input_rows[j]);
        free(output_rows[j]);
        free(temp_rows[j]);
      }
      free(input_rows);
      free(output_rows);
      free(temp_rows);
      bmp_close_process_io(&io);
      return;
    }

    if (fread(input_rows[i], sizeof(unsigned char), row_padded, image)
        != (size_t)row_padded) {
      printf("Error: no se pudo leer una fila completa de pixeles.\n");
      for (int j = 0; j <= i; j++) {
        free(input_rows[j]);
        free(output_rows[j]);
        free(temp_rows[j]);
      }
      free(input_rows);
      free(output_rows);
      free(temp_rows);
      bmp_close_process_io(&io);
      return;
    }
  }

  int k = kernel_size / 2;

  // Paso intermedio: desenfoque horizontal
  for (int y = 0; y < abs_height; y++) {
    for (int x = 0; x < width; x++) {
      // primero convertirlo a gris

      int sumB = 0, sumG = 0, sumR = 0, count = 0;

      for (int dx = -k; dx <= k; dx++) {
        int nx = x + dx;

        if (nx >= 0 && nx < width) {
          // transformarlo a gris


          int idx = nx * bytes_per_pixel;

          unsigned char b = input_rows[y][idx + 0];
          unsigned char g = input_rows[y][idx + 1];
          unsigned char r = input_rows[y][idx + 2];

          unsigned char gray = (unsigned char)((21 * r + 72 * g + 7 * b) / 100);
          input_rows[y][idx + 0] = gray;
          input_rows[y][idx + 1] = gray;
          input_rows[y][idx + 2] = gray;


          sumB += input_rows[y][idx + 0];
          sumG += input_rows[y][idx + 1];
          sumR += input_rows[y][idx + 2];
          count++;
        }
      }

      int index               = x * bytes_per_pixel;
      temp_rows[y][index + 0] = sumB / count;
      temp_rows[y][index + 1] = sumG / count;
      temp_rows[y][index + 2] = sumR / count;

      // Preservar el canal alfa si es de 32 bits
      if (bytes_per_pixel == 4) {
        temp_rows[y][index + 3] = input_rows[y][index + 3];
      }
    }

    // Copiar el padding
    for (int p = width * bytes_per_pixel; p < row_padded; p++) {
      temp_rows[y][p] = input_rows[y][p];
    }
  }

  // Paso final: desenfoque vertical
  for (int y = 0; y < abs_height; y++) {
    for (int x = 0; x < width; x++) {
      int sumB = 0, sumG = 0, sumR = 0, count = 0;

      for (int dy = -k; dy <= k; dy++) {
        int ny = y + dy;
        if (ny >= 0 && ny < abs_height) {
          int idx = x * bytes_per_pixel;
          sumB += temp_rows[ny][idx + 0];
          sumG += temp_rows[ny][idx + 1];
          sumR += temp_rows[ny][idx + 2];
          count++;
        }
      }

      int index                 = x * bytes_per_pixel;
      output_rows[y][index + 0] = sumB / count;
      output_rows[y][index + 1] = sumG / count;
      output_rows[y][index + 2] = sumR / count;

      // Preservar el canal alfa si es de 32 bits
      if (bytes_per_pixel == 4) {
        output_rows[y][index + 3] = temp_rows[y][index + 3];
      }
    }

    // Copiar el padding
    for (int p = width * bytes_per_pixel; p < row_padded; p++) {
      output_rows[y][p] = temp_rows[y][p];
    }
  }

  // Escritura final y limpieza
  for (int i = 0; i < abs_height; i++) {
    fwrite(output_rows[i], sizeof(unsigned char), row_padded, outputImage);
    free(input_rows[i]);
    free(temp_rows[i]);
    free(output_rows[i]);
  }

  // Escritura en el archivo de registro
  // FILE *outputLog = fopen("output_log.txt", "a");
  // if (outputLog != NULL) {
  //   fprintf(outputLog, "Función: desenfoque, con %s\n", input_path);
  //   fprintf(outputLog, "Localidades totales leídas: %d\n", width *
  //   abs_height); fprintf(
  //     outputLog, "Localidades totales escritas: %d\n", width * abs_height);
  //   fprintf(outputLog, "-------------------------------------\n");
  //   fclose(outputLog);
  // } else {
  //   fprintf(stderr,
  //           "Error: No se pudo crear o abrir el archivo de registro.\n");
  // }

  free(input_rows);
  free(temp_rows);
  free(output_rows);
  bmp_close_process_io(&io);
}


void desenfoque(const char           *input_path,
                const char           *output_suffix,
                int                   kernel_size,
                const bmp_image_info *bmp) {
  char output_name[256];
  bmp_build_output_name(
    input_path, output_suffix, output_name, sizeof(output_name));

  bmp_process_io io;
  if (!bmp_open_process_io(input_path, output_name, bmp, &io)) {
    return;
  }

  FILE *image       = io.image;
  FILE *outputImage = io.outputImage;

  int width           = bmp->width;
  int abs_height      = bmp->abs_height;
  int bytes_per_pixel = bmp->bytes_per_pixel;
  int row_padded      = bmp->row_padded;

  unsigned char **input_rows =
    (unsigned char **)malloc(abs_height * sizeof(unsigned char *));
  unsigned char **output_rows =
    (unsigned char **)malloc(abs_height * sizeof(unsigned char *));
  unsigned char **temp_rows =
    (unsigned char **)malloc(abs_height * sizeof(unsigned char *));

  for (int i = 0; i < abs_height; i++) {
    input_rows[i]  = (unsigned char *)malloc(row_padded);
    output_rows[i] = (unsigned char *)malloc(row_padded);
    temp_rows[i]   = (unsigned char *)malloc(row_padded);
    if (!input_rows[i] || !output_rows[i] || !temp_rows[i]) {
      printf("Error: no se pudo reservar memoria para las filas.\n");
      for (int j = 0; j <= i; j++) {
        free(input_rows[j]);
        free(output_rows[j]);
        free(temp_rows[j]);
      }
      free(input_rows);
      free(output_rows);
      free(temp_rows);
      bmp_close_process_io(&io);
      return;
    }

    if (fread(input_rows[i], sizeof(unsigned char), row_padded, image)
        != (size_t)row_padded) {
      printf("Error: no se pudo leer una fila completa de pixeles.\n");
      for (int j = 0; j <= i; j++) {
        free(input_rows[j]);
        free(output_rows[j]);
        free(temp_rows[j]);
      }
      free(input_rows);
      free(output_rows);
      free(temp_rows);
      bmp_close_process_io(&io);
      return;
    }
  }

  int k = kernel_size / 2;

  // Paso intermedio: desenfoque horizontal
  for (int y = 0; y < abs_height; y++) {
    for (int x = 0; x < width; x++) {
      int sumB = 0, sumG = 0, sumR = 0, count = 0;

      for (int dx = -k; dx <= k; dx++) {
        int nx = x + dx;
        if (nx >= 0 && nx < width) {
          int idx = nx * bytes_per_pixel;
          sumB += input_rows[y][idx + 0];
          sumG += input_rows[y][idx + 1];
          sumR += input_rows[y][idx + 2];
          count++;
        }
      }

      int index               = x * bytes_per_pixel;
      temp_rows[y][index + 0] = sumB / count;
      temp_rows[y][index + 1] = sumG / count;
      temp_rows[y][index + 2] = sumR / count;

      // Preservar canal Alpha si es de 32-bits
      if (bytes_per_pixel == 4) {
        temp_rows[y][index + 3] = input_rows[y][index + 3];
      }
    }

    // Copiar padding
    for (int p = width * bytes_per_pixel; p < row_padded; p++) {
      temp_rows[y][p] = input_rows[y][p];
    }
  }

  // Paso final: desenfoque vertical
  for (int y = 0; y < abs_height; y++) {
    for (int x = 0; x < width; x++) {
      int sumB = 0, sumG = 0, sumR = 0, count = 0;

      for (int dy = -k; dy <= k; dy++) {
        int ny = y + dy;
        if (ny >= 0 && ny < abs_height) {
          int idx = x * bytes_per_pixel;
          sumB += temp_rows[ny][idx + 0];
          sumG += temp_rows[ny][idx + 1];
          sumR += temp_rows[ny][idx + 2];
          count++;
        }
      }

      int index                 = x * bytes_per_pixel;
      output_rows[y][index + 0] = sumB / count;
      output_rows[y][index + 1] = sumG / count;
      output_rows[y][index + 2] = sumR / count;

      // Preservar canal Alpha si es de 32-bits
      if (bytes_per_pixel == 4) {
        output_rows[y][index + 3] = temp_rows[y][index + 3];
      }
    }

    // Copiar padding
    for (int p = width * bytes_per_pixel; p < row_padded; p++) {
      output_rows[y][p] = temp_rows[y][p];
    }
  }

  // Escritura final y limpieza
  for (int i = 0; i < abs_height; i++) {
    fwrite(output_rows[i], sizeof(unsigned char), row_padded, outputImage);
    free(input_rows[i]);
    free(temp_rows[i]);
    free(output_rows[i]);
  }

  // Escritura en archivo de registro
  // FILE *outputLog = fopen("output_log.txt", "a");
  // if (outputLog != NULL) {
  //   fprintf(outputLog, "Función: desenfoque, con %s\n", input_path);
  //   fprintf(outputLog, "Localidades totales leídas: %d\n", width *
  //   abs_height); fprintf(
  //     outputLog, "Localidades totales escritas: %d\n", width * abs_height);
  //   fprintf(outputLog, "-------------------------------------\n");
  //   fclose(outputLog);
  // } else {
  //   fprintf(stderr,
  //           "Error: No se pudo crear o abrir el archivo de registro.\n");
  // }

  free(input_rows);
  free(temp_rows);
  free(output_rows);
  bmp_close_process_io(&io);
}

#endif