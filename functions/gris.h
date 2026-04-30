#ifndef GRIS_H
#define GRIS_H

#include "bmp_utils.h"
#include <stdio.h>
#include <stdlib.h>

static inline void gris(const char           *input_path,
                        const char           *output_suffix,
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

  unsigned char *row = (unsigned char *)malloc((size_t)bmp->row_padded);
  if (!row) {
    printf("Error: no se pudo reservar memoria para la imagen.\n");
    bmp_close_process_io(&io);
    return;
  }

  for (int y = 0; y < bmp->abs_height; y++) {
    if (fread(row, sizeof(unsigned char), (size_t)bmp->row_padded, image)
        != (size_t)bmp->row_padded) {
      printf("Error: no se pudo leer una fila completa de pixeles.\n");
      free(row);
      bmp_close_process_io(&io);
      return;
    }

    for (int x = 0; x < bmp->width; x++) {
      int           idx = x * bmp->bytes_per_pixel;
      unsigned char b   = row[idx + 0];
      unsigned char g   = row[idx + 1];
      unsigned char r   = row[idx + 2];

      unsigned char gray = (unsigned char)((21 * r + 72 * g + 7 * b) / 100);
      row[idx + 0]       = gray;
      row[idx + 1]       = gray;
      row[idx + 2]       = gray;
    }

    if (fwrite(row, sizeof(unsigned char), (size_t)bmp->row_padded, outputImage)
        != (size_t)bmp->row_padded) {
      printf("Error: no se pudo escribir una fila completa en salida.\n");
      free(row);
      bmp_close_process_io(&io);
      return;
    }
  }

  free(row);
  bmp_close_process_io(&io);
  return;
}

#endif
