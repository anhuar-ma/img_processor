# Cómo ejecutar la aplicación

## Dependencias
- Tk en tu computadora
- Los paquetes `tkinterdnd2` y `pillow` de Python
- Open MPI instalado en el sistema

## Compilación
Compila el archivo `main_pararell.c` con el compilador de MPI:

```bash
mpicc -O2 main_pararell.c -o imgprocP
```

Si el clúster mezcla arquitecturas, compila una copia por arquitectura y publícala en las rutas que usa `execute.sh`, por ejemplo `imgprocP_linux_arm` y `imgprocP_linux_x86`.

## Ejecución con varios computadores
La aplicación ahora reparte las imágenes entre procesos MPI. Si no pasas rutas de imagen, el programa busca archivos `.bmp` dentro de `img_to_process/`.

Un ejemplo local es:

```bash
mpirun -np 3 --hostfile machinefile ./imgprocP img1.bmp img2.bmp --transforms vg vc hg hc dg dc
```

Si usas la GUI, ella invoca `mpirun` automáticamente, usa `execute.sh` como wrapper por nodo y lee `machinefile` para determinar la cantidad de procesos, aunque puedes sobrescribirla con `MPI_NP` o `MPI_PROCS`.

## Pruebas sin GUI
Para probar en terminal sin la interfaz gráfica, usa el binario MPI directamente en una sola máquina:

```bash
mpirun -np 2 ./imgprocP --transforms vg dc
```

Si quieres probar el escenario distribuido y con arquitecturas mezcladas, usa el wrapper:

```bash
mpirun -np 3 --hostfile machinefile ./execute.sh --transforms vg dc
```

En ambos casos, si no pasas imágenes en la línea de comandos, el programa toma los `.bmp` de `img_to_process/`.

## Wrapper por arquitectura
`execute.sh` detecta si el nodo es `aarch64` o `x86_64` y ejecuta el binario correcto del procesador de imágenes. Eso permite que `mpirun` arranque el mismo script en todos los nodos, mientras cada nodo carga su binario nativo.

El wrapper asume que los binarios viven en `/mirror/` con estos nombres:
- `/mirror/imgprocP_linux_arm`
- `/mirror/imgprocP_linux_x86`

Si tus rutas reales son otras, ajusta `execute.sh` en cada nodo.

## Ejecutar la GUI

```bash
python gui.py
```


