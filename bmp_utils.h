#ifndef BMP_UTILS_H
#define BMP_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  unsigned char  header[54];
  unsigned char *extra_header;
  int            extra_header_size;
  int            data_offset;
  int            width;
  int            height;
  int            abs_height;
  short          bpp;
  int            bytes_per_pixel;
  int            row_padded;
} bmp_image_info;

typedef struct {
  FILE *image;
  FILE *outputImage;
  char  output_path[256];
} bmp_process_io;

static inline int bmp_read_info(FILE *image, bmp_image_info *info) {
  if (!image || !info) {
    return 0;
  }

  memset(info, 0, sizeof(*info));

  // Leer primero la región fija del encabezado BMP.
  if (fread(info->header, sizeof(unsigned char), 54, image) != 54) {
    return 0;
  }

  // Analizar los metadatos principales desde los desplazamientos estándar del
  // BMP.
  info->data_offset     = *(int *)&info->header[10];
  info->width           = *(int *)&info->header[18];
  info->height          = *(int *)&info->header[22];
  info->bpp             = *(short *)&info->header[28];
  info->bytes_per_pixel = info->bpp / 8;
  info->abs_height      = abs(info->height);

  // Rechazar temprano las imágenes mal formadas.
  if (info->width <= 0 || info->abs_height <= 0 || info->bytes_per_pixel <= 0) {
    return 0;
  }

  info->extra_header_size = info->data_offset - 54;
  if (info->extra_header_size < 0) {
    return 0;
  }

  if (info->extra_header_size > 0) {
    // Algunas variantes de BMP agregan metadatos antes de los pixeles;
    // conservarlos.
    info->extra_header = (unsigned char *)malloc(info->extra_header_size);
    if (!info->extra_header) {
      return 0;
    }

    if (fread(info->extra_header,
              sizeof(unsigned char),
              info->extra_header_size,
              image)
        != (size_t)info->extra_header_size) {
      free(info->extra_header);
      info->extra_header = NULL;
      return 0;
    }
  }

  // Las filas BMP se rellenan hasta un límite de 4 bytes.
  info->row_padded = (info->width * info->bytes_per_pixel + 3) & (~3);
  return 1;
}

static inline int bmp_write_header(FILE                 *outputImage,
                                   const bmp_image_info *info) {
  if (!outputImage || !info) {
    return 0;
  }

  // Escribir el encabezado fijo y los metadatos extra opcionales exactamente
  // como se leyeron.
  if (fwrite(info->header, sizeof(unsigned char), 54, outputImage) != 54) {
    return 0;
  }

  if (info->extra_header_size > 0) {
    if (fwrite(info->extra_header,
               sizeof(unsigned char),
               info->extra_header_size,
               outputImage)
        != (size_t)info->extra_header_size) {
      return 0;
    }
  }

  return 1;
}

static inline void bmp_free_info(bmp_image_info *info) {
  if (!info) {
    return;
  }

  // Liberar el búfer opcional de metadatos preservados.
  if (info->extra_header) {
    free(info->extra_header);
    info->extra_header = NULL;
  }
}

static inline int bmp_open_process_io(const char           *input_path,
                                      const char           *name_output,
                                      const bmp_image_info *bmp,
                                      bmp_process_io       *io) {
  if (!bmp || !io) {
    printf("Error: metadata BMP no disponible.\n");
    return 0;
  }

  memset(io, 0, sizeof(*io));

  io->image = fopen(input_path, "rb");
  if (!io->image) {
    printf("Error: No se pudo abrir la imagen original.\n");
    return 0;
  }

  snprintf(
    io->output_path, sizeof(io->output_path), "./img/%s.bmp", name_output);
  io->outputImage = fopen(io->output_path, "wb");
  if (!io->outputImage) {
    printf("Error: No se pudo crear %s. ¿Existe la carpeta ./img/?\n",
           io->output_path);
    fclose(io->image);
    io->image = NULL;
    return 0;
  }

  if (!bmp_write_header(io->outputImage, bmp)) {
    printf("Error: no se pudo escribir el encabezado BMP de salida.\n");
    fclose(io->image);
    fclose(io->outputImage);
    io->image       = NULL;
    io->outputImage = NULL;
    return 0;
  }

  if (fseek(io->image, bmp->data_offset, SEEK_SET) != 0) {
    printf("Error: no se pudo posicionar el archivo en los pixeles.\n");
    fclose(io->image);
    fclose(io->outputImage);
    io->image       = NULL;
    io->outputImage = NULL;
    return 0;
  }

  return 1;
}

static inline void bmp_close_process_io(bmp_process_io *io) {
  if (!io) {
    return;
  }

  if (io->image) {
    fclose(io->image);
    io->image = NULL;
  }

  if (io->outputImage) {
    fclose(io->outputImage);
    io->outputImage = NULL;
  }
}


#endif
