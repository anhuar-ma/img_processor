"""
Requiere: pip install tkinterdnd2
Ejecutar:  python gui.py
El ejecutable 'imgprocP' debe estar en el mismo directorio que este script.
"""

import os
import subprocess
import threading
import tkinter as tk
from tkinterdnd2 import TkinterDnD, DND_FILES

# Colores
BG = "#2d2d2d"   # fondo general
PANEL = "#1a1a1a"   # panel fotos
ACCENT = "#4a9eff"   # color de selección
FG = "#ffffff"   # texto principal
SUBFG = "#888888"   # texto secundario
FIELD = "#3c3c3c"   # fondo de campos de entrada
BTN = "#2D34FF"   # fondo de botones


# Clase principal
class App(TkinterDnD.Tk):

    def __init__(self):
        super().__init__()

        self.title("Procesamiento de imágenes")
        self.configure(bg=BG)
        self.resizable(False, False)

        # Estado 
        self.images: list[str] = []   # rutas absolutas de los archivos .bmp

        # Variables checkboxes
        self.var_vg = tk.BooleanVar()
        self.var_vc = tk.BooleanVar()
        self.var_hg = tk.BooleanVar()
        self.var_hc = tk.BooleanVar()
        self.var_dg = tk.BooleanVar()
        self.var_dc = tk.BooleanVar()

        # Variables de kernel
        self.kernel_dg = tk.StringVar(value="16")
        self.kernel_dc = tk.StringVar(value="16")

        # Variables de salida
        self.tiempo_var = tk.StringVar()
        self.ruta_var   = tk.StringVar()

        self._build_ui()

    # Construcción de la interfaz
    def _build_ui(self):
        root = tk.Frame(self, bg=BG, padx=18, pady=18)
        root.pack(fill="both", expand=True)

        # Panel izquierdo y controles derecha
        top = tk.Frame(root, bg=BG)
        top.pack(fill="both", expand=True)

        self._build_drop_panel(top)
        self._build_controls(top)

        # Separador
        tk.Frame(root, bg="#444444", height=1).pack(fill="x", pady=(14, 0))

        # Tiempo, ruta, botón
        self._build_bottom(root)

    # Panel para arrastrar y soltar imágenes
    def _build_drop_panel(self, parent):
        frame = tk.Frame(parent, bg=PANEL, width=265, height=310)
        frame.pack(side="left", fill="y", padx=(0, 18))
        frame.pack_propagate(False)

        hint = tk.Label(
            frame,
            text="Arrastra imágenes\nmáximo 10\n.bmp",
            bg=PANEL, fg=SUBFG,
            font=("Helvetica", 12),
            justify="center",
        )
        hint.pack(pady=(16, 6))

        # Lista de archivos cargados
        self.listbox = tk.Listbox(
            frame,
            bg=PANEL, fg=FG,
            selectbackground=ACCENT,
            selectforeground=FG,
            font=("Helvetica", 10),
            relief="flat", bd=0,
            activestyle="none",
            height=12,
        )
        self.listbox.pack(fill="both", expand=True, padx=8, pady=(0, 8))

        # Instrucción de eliminación
        tk.Label(
            frame,
            text="Click derecho para quitar archivo",
            bg=PANEL, fg=SUBFG,
            font=("Helvetica", 8),
        ).pack(pady=(0, 6))

        # Clic derecho elimina el elemento seleccionado
        self.listbox.bind("<Button-2>", self._remove_file)   # macOS
        self.listbox.bind("<Button-3>", self._remove_file)   # Windows/Linux

        # Registrar drag-and-drop en todo el panel y sus hijos
        for widget in (frame, hint, self.listbox):
            widget.drop_target_register(DND_FILES)
            widget.dnd_bind("<<Drop>>", self._on_drop)

    # Panel de controles (derecha)
    def _build_controls(self, parent):
        frame = tk.Frame(parent, bg=BG)
        frame.pack(side="left", fill="both", expand=True)

        transforms = [
            ("1- Vertical escala de grises",    self.var_vg, None),
            ("2- Vertical escala a colores",     self.var_vc, None),
            ("3- Horizontal escala de grises",   self.var_hg, None),
            ("4- Horizontal escala a colores",   self.var_hc, None),
            ("5- Desenfoque escala de grises",   self.var_dg, self.kernel_dg),
            ("6- Desenfoque escala a colores",   self.var_dc, self.kernel_dc),
        ]

        for label, var, kernel_var in transforms:
            row = tk.Frame(frame, bg=BG)
            row.pack(fill="x", pady=5)

            cb = tk.Checkbutton(
                row,
                text=label,
                variable=var,
                bg=BG, fg=FG,
                selectcolor=PANEL,
                activebackground=BG,
                activeforeground=FG,
                font=("Helvetica", 12),
                cursor="hand",
            )
            cb.pack(side="left")

            if kernel_var is not None:
                tk.Entry(
                    row,
                    textvariable=kernel_var,
                    width=5,
                    bg=FIELD, fg=FG,
                    relief="flat",
                    font=("Helvetica", 12),
                    insertbackground=FG,
                    justify="center",
                ).pack(side="left", padx=(12, 4))

                tk.Label(
                    row,
                    text="Kernel",
                    bg=BG, fg=FG,
                    font=("Helvetica", 12),
                ).pack(side="left")

        # Botón "Todas"
        spacer = tk.Frame(frame, bg=BG, height=10)
        spacer.pack()

        todas_row = tk.Frame(frame, bg=BG)
        todas_row.pack(fill="x")

        tk.Button(
            todas_row,
            text="  Todas  ",
            bg=BTN, fg=FG,
            activebackground="#4D4D4D",
            activeforeground=FG,
            relief="flat",
            font=("Helvetica", 12),
            cursor="hand",
            command=self._select_all,
            padx=6, pady=4,
        ).pack(side="left")

        tk.Label(
            todas_row,
            text="  Se seleccionan todas las\n  transformaciones de imágenes",
            bg=BG, fg=SUBFG,
            font=("Helvetica", 10),
            justify="left",
        ).pack(side="left")

    # ── Sección inferior: tiempo, ruta, ejecutar ─────────────────────────────
    def _build_bottom(self, parent):
        bottom = tk.Frame(parent, bg=BG)
        bottom.pack(fill="x", pady=(14, 0))

        # Izquierda: campos de información
        info = tk.Frame(bottom, bg=BG)
        info.pack(side="left", fill="x", expand=True)

        tk.Label(info, text="Tiempo de ejecución",
                 bg=BG, fg=FG, font=("Helvetica", 11)).pack(anchor="w")
        tk.Entry(
            info,
            textvariable=self.tiempo_var,
            width=28,
            bg=FIELD, fg=FG,
            relief="flat",
            font=("Helvetica", 11),
            state="readonly",
            readonlybackground=FIELD,
        ).pack(anchor="w", pady=(3, 10))

        tk.Label(info, text="Ruta de archivos",
                 bg=BG, fg=FG, font=("Helvetica", 11)).pack(anchor="w")
        tk.Entry(
            info,
            textvariable=self.ruta_var,
            width=28,
            bg=FIELD, fg=FG,
            relief="flat",
            font=("Helvetica", 11),
            state="readonly",
            readonlybackground=FIELD,
        ).pack(anchor="w", pady=(3, 0))

        # botón ejecutar y logo
        right = tk.Frame(bottom, bg=BG)
        right.pack(side="right", anchor="s")

        self.exec_btn = tk.Button(
            right,
            text="  Ejecutar  ",
            bg=BTN, fg=FG,
            activebackground="#424242",
            activeforeground=FG,
            relief="flat",
            font=("Helvetica", 14),
            cursor="hand",
            command=self._execute,
            padx=12, pady=6,
        )
        self.exec_btn.pack(pady=(0, 10))

        # Logo del Tec
       

    # Lógica de drag-and-drop
    def _on_drop(self, event):
        # tkinterdnd2 devuelve paths entre llaves si tienen espacios
        raw_paths = self.tk.splitlist(event.data)
        added = 0

        for path in raw_paths:
            path = path.strip()
            if not path.lower().endswith(".bmp"):
                continue
            if path in self.images:
                continue
            if len(self.images) >= 10:
                break
            self.images.append(path)
            self.listbox.insert("end", os.path.basename(path))
            added += 1

        if added:
            self.ruta_var.set("")
            self.tiempo_var.set("")

    def _remove_file(self, event):
        sel = self.listbox.curselection()
        if sel:
            idx = sel[0]
            self.listbox.delete(idx)
            self.images.pop(idx)

    # Seleccionar todas
    def _select_all(self):
        for var in (self.var_vg, self.var_vc, self.var_hg,
                    self.var_hc, self.var_dg, self.var_dc):
            var.set(True)

    # Ejecutar procesamiento 
    def _execute(self):
        # Validaciones
        if not self.images:
            self.tiempo_var.set("Sin imágenes cargadas")
            return

        selected = []
        if self.var_vg.get(): selected.append("vg")
        if self.var_vc.get(): selected.append("vc")
        if self.var_hg.get(): selected.append("hg")
        if self.var_hc.get(): selected.append("hc")
        if self.var_dg.get(): selected.append("dg")
        if self.var_dc.get(): selected.append("dc")

        if not selected:
            self.tiempo_var.set("Selecciona al menos una transformación")
            return

        # Kernels
        try:
            k_dg = max(1, int(self.kernel_dg.get()))
        except ValueError:
            k_dg = 16
        try:
            k_dc = max(1, int(self.kernel_dc.get()))
        except ValueError:
            k_dc = 16

        # Deshabilitar botón mientras procesa
        self.exec_btn.config(state="disabled", text="  Procesando...  ")
        self.tiempo_var.set("Procesando…")
        self.ruta_var.set("")

        threading.Thread(
            target=self._run,
            args=(selected, k_dg, k_dc),
            daemon=True,
        ).start()

    def _run(self, selected: list, k_dg: int, k_dc: int):
        script_dir = os.path.dirname(os.path.abspath(__file__))
        executable = os.path.join(script_dir, "imgprocP")
        img_dir    = os.path.join(script_dir, "img")

        # Crear carpeta de salida si no existe
        os.makedirs(img_dir, exist_ok=True)

        # Construir comando
        cmd = [executable] + self.images + ["--transforms"] + selected

        if "dg" in selected:
            cmd += ["--kernel-dg", str(k_dg)]
        if "dc" in selected:
            cmd += ["--kernel-dc", str(k_dc)]

        try:
            result = subprocess.run(
                cmd,
                cwd=script_dir,
                capture_output=True,
                text=True,
                timeout=600,
            )

            tiempo = "—"
            for line in result.stdout.splitlines():
                if line.startswith("TIEMPO:"):
                    t = float(line.split(":")[1])
                    tiempo = f"{t:.4f} s"

            # Volver al hilo principal para actualizar la UI
            self.after(0, self._on_done, tiempo, img_dir)

        except subprocess.TimeoutExpired:
            self.after(0, self._on_error, "Tiempo de espera agotado (>10 min)")
        except FileNotFoundError:
            self.after(0, self._on_error,
                       f"Ejecutable no encontrado: {executable}\n"
                       "Compila primero con:\n"
                       "gcc-14 -O2 -fopenmp main_pararell.c -o imgprocP")
        except Exception as e:
            self.after(0, self._on_error, str(e))

    def _on_done(self, tiempo: str, img_dir: str):
        self.tiempo_var.set(tiempo)
        self.ruta_var.set(img_dir)
        self.exec_btn.config(state="normal", text="  Ejecutar  ")

    def _on_error(self, msg: str):
        self.tiempo_var.set(f"Error: {msg}")
        self.ruta_var.set("")
        self.exec_btn.config(state="normal", text="  Ejecutar  ")

if __name__ == "__main__":
    app = App()
    app.mainloop()
