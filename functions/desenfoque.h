#ifndef DESENFOQUE_H
#define DESENFOQUE_H

#include "bmp_utils.h"
#include <stdint.h>
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

  // Build summed-area table (integral image) for grayscale values
  size_t H = (size_t)abs_height;
  size_t W = (size_t)width;

  uint64_t *sumGray = (uint64_t *)calloc((H + 1) * (W + 1), sizeof(uint64_t));
  if (!sumGray) {
    printf("Error: no se pudo reservar memoria para sumGray.\n");
    for (int i = 0; i < abs_height; i++) {
      free(input_rows[i]);
      free(output_rows[i]);
      free(temp_rows[i]);
    }
    free(input_rows);
    free(output_rows);
    free(temp_rows);
    bmp_close_process_io(&io);
    return;
  }

  for (size_t y = 0; y < H; y++) {
    for (size_t x = 0; x < W; x++) {
      int           idx  = (int)(x * bytes_per_pixel);
      unsigned char b    = input_rows[y][idx + 0];
      unsigned char g    = input_rows[y][idx + 1];
      unsigned char r    = input_rows[y][idx + 2];
      unsigned char gray = (unsigned char)((21 * r + 72 * g + 7 * b) / 100);

      // Keep grayscale in input buffer (helps preserve alpha/padding handling
      // below)
      input_rows[y][idx + 0] = gray;
      input_rows[y][idx + 1] = gray;
      input_rows[y][idx + 2] = gray;

      size_t ii   = (y + 1) * (W + 1) + (x + 1);
      sumGray[ii] = sumGray[(y + 1) * (W + 1) + x]
                    + sumGray[y * (W + 1) + (x + 1)] - sumGray[y * (W + 1) + x]
                    + (uint64_t)gray;
    }
  }

  // Apply box blur using summed-area table
  for (size_t y = 0; y < H; y++) {
    for (size_t x = 0; x < W; x++) {
      size_t y1 = (y > (size_t)k) ? y - (size_t)k : 0;
      size_t x1 = (x > (size_t)k) ? x - (size_t)k : 0;
      size_t y2 = (y + (size_t)k < H) ? y + (size_t)k : H - 1;
      size_t x2 = (x + (size_t)k < W) ? x + (size_t)k : W - 1;

      uint64_t sum = sumGray[(y2 + 1) * (W + 1) + (x2 + 1)]
                     - sumGray[y1 * (W + 1) + (x2 + 1)]
                     - sumGray[(y2 + 1) * (W + 1) + x1]
                     + sumGray[y1 * (W + 1) + x1];

      uint64_t      area = (uint64_t)(y2 - y1 + 1) * (uint64_t)(x2 - x1 + 1);
      unsigned char avg  = (unsigned char)(sum / area);

      int index                 = (int)(x * bytes_per_pixel);
      output_rows[y][index + 0] = avg;
      output_rows[y][index + 1] = avg;
      output_rows[y][index + 2] = avg;
      if (bytes_per_pixel == 4) {
        output_rows[y][index + 3] = input_rows[y][index + 3];
      }
    }

    // Copiar el padding
    for (int p = width * bytes_per_pixel; p < row_padded; p++) {
      output_rows[y][p] = input_rows[y][p];
    }
  }

  free(sumGray);

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

  // Build summed-area tables (integral images) for each color channel
  size_t H = (size_t)abs_height;
  size_t W = (size_t)width;

  uint64_t *sumB = (uint64_t *)calloc((H + 1) * (W + 1), sizeof(uint64_t));
  uint64_t *sumG = (uint64_t *)calloc((H + 1) * (W + 1), sizeof(uint64_t));
  uint64_t *sumR = (uint64_t *)calloc((H + 1) * (W + 1), sizeof(uint64_t));

  if (!sumB || !sumG || !sumR) {
    printf("Error: no se pudo reservar memoria para las tablas de suma.\n");
    free(sumB);
    free(sumG);
    free(sumR);
    for (int i = 0; i < abs_height; i++) {
      free(input_rows[i]);
      free(output_rows[i]);
      free(temp_rows[i]);
    }
    free(input_rows);
    free(output_rows);
    free(temp_rows);
    bmp_close_process_io(&io);
    return;
  }

  for (size_t y = 0; y < H; y++) {
    for (size_t x = 0; x < W; x++) {
      int      idx = (int)(x * bytes_per_pixel);
      uint64_t vb  = (uint64_t)input_rows[y][idx + 0];
      uint64_t vg  = (uint64_t)input_rows[y][idx + 1];
      uint64_t vr  = (uint64_t)input_rows[y][idx + 2];

      size_t ii = (y + 1) * (W + 1) + (x + 1);
      sumB[ii]  = sumB[(y + 1) * (W + 1) + x] + sumB[y * (W + 1) + (x + 1)]
                  - sumB[y * (W + 1) + x] + vb;
      sumG[ii]  = sumG[(y + 1) * (W + 1) + x] + sumG[y * (W + 1) + (x + 1)]
                  - sumG[y * (W + 1) + x] + vg;
      sumR[ii]  = sumR[(y + 1) * (W + 1) + x] + sumR[y * (W + 1) + (x + 1)]
                  - sumR[y * (W + 1) + x] + vr;
    }
  }

  // Apply box blur using summed-area tables
  for (size_t y = 0; y < H; y++) {
    for (size_t x = 0; x < W; x++) {
      size_t y1 = (y > (size_t)k) ? y - (size_t)k : 0;
      size_t x1 = (x > (size_t)k) ? x - (size_t)k : 0;
      size_t y2 = (y + (size_t)k < H) ? y + (size_t)k : H - 1;
      size_t x2 = (x + (size_t)k < W) ? x + (size_t)k : W - 1;

      uint64_t sum_b =
        sumB[(y2 + 1) * (W + 1) + (x2 + 1)] - sumB[y1 * (W + 1) + (x2 + 1)]
        - sumB[(y2 + 1) * (W + 1) + x1] + sumB[y1 * (W + 1) + x1];
      uint64_t sum_g =
        sumG[(y2 + 1) * (W + 1) + (x2 + 1)] - sumG[y1 * (W + 1) + (x2 + 1)]
        - sumG[(y2 + 1) * (W + 1) + x1] + sumG[y1 * (W + 1) + x1];
      uint64_t sum_r =
        sumR[(y2 + 1) * (W + 1) + (x2 + 1)] - sumR[y1 * (W + 1) + (x2 + 1)]
        - sumR[(y2 + 1) * (W + 1) + x1] + sumR[y1 * (W + 1) + x1];

      uint64_t area = (uint64_t)(y2 - y1 + 1) * (uint64_t)(x2 - x1 + 1);

      int index                 = (int)(x * bytes_per_pixel);
      output_rows[y][index + 0] = (unsigned char)(sum_b / area);
      output_rows[y][index + 1] = (unsigned char)(sum_g / area);
      output_rows[y][index + 2] = (unsigned char)(sum_r / area);
      if (bytes_per_pixel == 4) {
        output_rows[y][index + 3] = input_rows[y][index + 3];
      }
    }

    // Copiar padding
    for (int p = width * bytes_per_pixel; p < row_padded; p++) {
      output_rows[y][p] = input_rows[y][p];
    }
  }

  free(sumB);
  free(sumG);
  free(sumR);

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