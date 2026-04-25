# Correr sin paralelizar

`gcc main_pararell.c -o imgproc && ./imgproc`    

# Correr paralelizado


`gcc -O2 -fopenmp main_pararell.c -o imgprocP && ./imgprocP`

## Nota

Necesita existir la carpeta img en este directorio porque ahi se generan las imagenes