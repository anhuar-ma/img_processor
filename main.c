#include "bmp_utils.h"
#include "desenfoque.h"
#include "gris.h"
#include "inv_hz.h"
#include "inv_vt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
  const char *input_file = "sample.bmp";
  FILE       *image      = fopen(input_file, "rb");

  if (!image) {
    printf("Error: No se pudo abrir la imagen original.\n");
    return 1;
  }

  bmp_image_info bmp;
  if (!bmp_read_info(image, &bmp)) {
    printf("Error: encabezado BMP invalido o incompleto.\n");
    fclose(image);
    return 1;
  }
  fclose(image);

  printf("Processing image: %s\n", input_file);

  // Aplicar filtro de escala de grises
  printf("Applying grayscale filter...\n");
  gris(input_file, "img_gris.bmp", &bmp);

  // Aplicar filtro de desenfoque
  printf("Applying blur filter...\n");
  desenfoque(input_file, "img_desenfoque", 16, &bmp);

  // Aplicar filtro de desenfoque en gris
  printf("Applying gray blur filter...\n");
  desenfoque_gris(input_file, "img_desenfoque_gris", 16, &bmp);


  printf("Applying inverting image...\n");
  inv_hz_color(input_file, "img_inverted", &bmp);


  printf("Applying inverting image gray...\n");
  inv_hz_gris(input_file, "img_inverted_gris", &bmp);

  printf("Applying inverting vertically  image...\n");
  inv_vt_color(input_file, "img_inverted_vt", &bmp);


  printf("Applying inverting vertially image gray...\n");
  inv_vt_gris(input_file, "img_inverted_gris_vt", &bmp);

  bmp_free_info(&bmp);

  printf("Processing complete!\n");
  printf("Output files created:\n");
  printf("  - img_gris.bmp\n");
  printf("  - ./img/img_desenfoque.bmp\n");

  return 0;
}
