import os
import json
import shlex
import shutil
import subprocess
import threading
import time
import sys
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from tkinterdnd2 import DND_FILES, TkinterDnD

from PIL import Image, ImageTk

# Estilo visual del Dashboard 
BG       = "#2d2d2d"
PANEL    = "#1a1a1a"
ACCENT   = "#4a9eff"
FG       = "#ffffff"
SUBFG    = "#888888"
FIELD    = "#3c3c3c"
BTN      = "#4a4a4a"


# Clase principal
class App(TkinterDnD.Tk):
    MAX_INPUT_FILES = 600
    
    #  Configuración para cálculos de costos y red MPI 
    LOCAL_POWER_WATTS = 250.0
    ELECTRICITY_RATE_USD_PER_KWH = 0.20
    AWS_HOURLY_RATE_USD = 0.35
    WORKING_HOURS_PER_YEAR = 8 * 5 * 52
    MPI_OOB_IF_INCLUDE = "tailscale0"
    MPI_BTL_IF_INCLUDE = "tailscale0"
    MPI_BTL_DISABLE_FAMILY = "6"

    def __init__(self):
        super().__init__()

        self.title("Procesamiento de imágenes")
        self.configure(bg=BG)
        self.geometry("700x500")
        self.resizable(True, True)

        #  Estado interno de la aplicación y métricas 
        self.images: list[str] = []
        self.input_dir = ""
        script_dir = os.path.dirname(os.path.abspath(__file__))
        self.output_dir = os.path.join(script_dir, "img")
        self.machine_hosts: list[tuple[str, int]] = []
        self.active_machinefile = ""
        self.last_log_path = ""
        self.last_throughput_bps = None
        self.current_total_bytes = 0
        self._progress_after_id = None
        self._run_started_at = None
        self._estimated_total_seconds = None
        self._run_in_progress = False

        self.var_vg = tk.BooleanVar()
        self.var_vc = tk.BooleanVar()
        self.var_hg = tk.BooleanVar()
        self.var_hc = tk.BooleanVar()
        self.var_dg = tk.BooleanVar()
        self.var_dc = tk.BooleanVar()

        self.kernel_dg = tk.StringVar(value="16")
        self.kernel_dc = tk.StringVar(value="16")

        self.tiempo_var = tk.StringVar()
        self.input_var = tk.StringVar(value="Ninguna Carpeta Seleccionada")
        self.output_var = tk.StringVar(value=self.output_dir)
        self.eta_var = tk.StringVar(value="ETA: --:--:--")
        self.bps_var = tk.StringVar(value="0.00e+00 B/s")
        self.cost_local_var = tk.StringVar(value="$0.00 / año")
        self.cost_aws_var = tk.StringVar(value="$0.00 / año")
        self.cost_diff_var = tk.StringVar(value="$0.00 / año")
        self.cluster_var = tk.StringVar(value="")
        self.workload_var = tk.StringVar(value="0 archivos | 0 B")
        self.total_var = tk.StringVar(value="0.0 %")
        self.progress_var = tk.DoubleVar(value=0.0)

        self.logo_img = self._load_logo(size=60)

        self._build_menu()
        self._build_ui()
        self._refresh_machinefile_view()

    def _load_logo(self, size=60):
        """Carga y redimensiona el logo institucional."""
        script_dir = os.path.dirname(os.path.abspath(__file__))
        logo_path  = os.path.join(script_dir, "./assets/logos/tec_logo.png")
        try:
            img = Image.open(logo_path).convert("RGBA")
            img = img.resize((size, size), Image.LANCZOS)
            return ImageTk.PhotoImage(img)
        except Exception:
            return None

    def _machinefile_slots(self, machinefile_path: str) -> int:
        try:
            total = 0
            with open(machinefile_path, "r", encoding="utf-8") as handle:
                for raw_line in handle:
                    line = raw_line.strip()
                    if not line or line.startswith("#"):
                        continue
                    for token in line.split():
                        if token.startswith("slots="):
                            total += int(token.split("=", 1)[1])
                            break
            return total if total > 0 else 1
        except Exception:
            return 1

    def _machinefile_hosts(self, machinefile_path: str) -> list[tuple[str, int]]:
        hosts: list[tuple[str, int]] = []
        try:
            with open(machinefile_path, "r", encoding="utf-8") as handle:
                for raw_line in handle:
                    line = raw_line.strip()
                    if not line or line.startswith("#"):
                        continue

                    parts = line.split()
                    host = parts[0]
                    slots = 1
                    for token in parts[1:]:
                        if token.startswith("slots="):
                            try:
                                slots = int(token.split("=", 1)[1])
                            except ValueError:
                                slots = 1
                            break
                    hosts.append((host, max(1, slots)))
        except Exception:
            return []
        return hosts

    def _format_bytes(self, size: int) -> str:
        units = ["B", "KB", "MB", "GB", "TB"]
        value = float(max(0, size))
        unit_index = 0
        while value >= 1024.0 and unit_index < len(units) - 1:
            value /= 1024.0
            unit_index += 1
        return f"{value:.2f} {units[unit_index]}"

    def _format_duration(self, seconds: float) -> str:
        seconds = max(0.0, float(seconds))
        whole = int(seconds)
        hours = whole // 3600
        minutes = (whole % 3600) // 60
        remaining = whole % 60
        return f"{hours:02d}:{minutes:02d}:{remaining:02d}"

    def _scientific_bps(self, bytes_per_second: float) -> str:
        return f"{bytes_per_second:.2e} B/s"

    def _annual_costs(self) -> tuple[float, float, float]:
        #  Valores fijos para la comparativa económica 
        local = 30313
        aws = 77246
        return local, aws, local - aws

    def _selected_total_bytes(self, paths: list[str]) -> int:
        total = 0
        for path in paths:
            try:
                total += os.path.getsize(path)
            except OSError:
                continue
        return total

    def _refresh_machinefile_view(self):
        script_dir = os.path.dirname(os.path.abspath(__file__))
        machinefile = os.path.join(script_dir, "machinefile")
        self.machine_hosts = (
            self._machinefile_hosts(machinefile) if os.path.exists(machinefile) else []
        )

        if self.machine_hosts:
            summary = ", ".join(f"{host} ({slots})" for host, slots in self.machine_hosts)
        else:
            summary = "No se encontró machinefile"
        self.cluster_var.set(summary)

        if hasattr(self, "cluster_listbox"):
            self.cluster_listbox.delete(0, "end")
            if self.machine_hosts:
                for host, slots in self.machine_hosts:
                    self.cluster_listbox.insert("end", f"{host}  |  {slots} slots")
            else:
                self.cluster_listbox.insert("end", "Sin computadoras configuradas")

    def _set_cluster_hosts(self, hosts: list[tuple[str, int]], summary_prefix: str):
        self.machine_hosts = hosts
        if hosts:
            summary = ", ".join(f"{host} ({slots})" for host, slots in hosts)
            self.cluster_var.set(f"{summary_prefix}: {summary}")
            if hasattr(self, "cluster_listbox"):
                self.cluster_listbox.delete(0, "end")
                for host, slots in hosts:
                    self.cluster_listbox.insert("end", f"{host}  |  {slots} slots")
        else:
            self.cluster_var.set(f"{summary_prefix}: sin hosts activos")
            if hasattr(self, "cluster_listbox"):
                self.cluster_listbox.delete(0, "end")
                self.cluster_listbox.insert("end", "Sin computadoras activas")

    def _prepare_machinefile(self, script_dir: str) -> tuple[str, list[tuple[str, int]]]:
        """Invoca el script bash para filtrar nodos inactivos antes de la ejecución."""
        source_machinefile = os.path.join(script_dir, "machinefile_all")
        filtered_machinefile = os.path.join(script_dir, "machinefile")
        filter_script = os.path.join(script_dir, "create_machinefile.sh")
        if not os.path.exists(filter_script):
            raise FileNotFoundError(f"No existe {filter_script}")

        os.makedirs(os.path.dirname(filtered_machinefile), exist_ok=True)

        result = subprocess.run(
            ["bash", filter_script, source_machinefile, filtered_machinefile],
            cwd=script_dir,
            capture_output=True,
            text=True,
        )

        payload = None
        if result.stdout.strip():
            try:
                payload = json.loads(result.stdout.strip().splitlines()[-1])
            except json.JSONDecodeError as exc:
                raise RuntimeError(
                    f"create_machinefile no devolvió JSON válido: {exc}\n{result.stdout}\n{result.stderr}"
                ) from exc

        if result.returncode != 0:
            message = payload.get("error") if isinstance(payload, dict) else None
            if not message:
                message = (result.stderr or result.stdout or "Error desconocido al filtrar machinefile").strip()
            raise RuntimeError(message)

        if not isinstance(payload, dict):
            raise RuntimeError("create_machinefile no devolvió datos estructurados")

        active_hosts_raw = payload.get("active_hosts", [])
        active_hosts: list[tuple[str, int]] = []
        for item in active_hosts_raw:
            host = str(item.get("host", "")).strip()
            slots = int(item.get("slots", 1)) if host else 0
            if host:
                active_hosts.append((host, max(1, slots)))

        if not active_hosts:
            raise RuntimeError("No hay hosts activos en el machinefile")

        return str(payload.get("output_file", filtered_machinefile)), active_hosts

    def _write_execution_log(
        self,
        log_path: str,
        *,
        command: list[str],
        machinefile_source: str,
        machinefile_active: str,
        active_hosts: list[tuple[str, int]],
        selected: list[str],
        k_dg: int,
        k_dc: int,
        result: subprocess.CompletedProcess,
        started_at: float,
        finished_at: float,
    ) -> None:
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        task_lines = [
            line for line in result.stdout.splitlines()
            if line.startswith("TASK:") or line.startswith("SUMMARY:")
        ]

        with open(log_path, "w", encoding="utf-8") as handle:
            handle.write("MPI image processing log\n")
            handle.write(f"Started: {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(started_at))}\n")
            handle.write(f"Finished: {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(finished_at))}\n")
            handle.write(f"Elapsed: {finished_at - started_at:.4f} s\n")
            handle.write(f"Command: {' '.join(shlex.quote(part) for part in command)}\n")
            handle.write(f"Source machinefile: {machinefile_source}\n")
            handle.write(f"Active machinefile: {machinefile_active}\n")
            handle.write(f"Active hosts: {len(active_hosts)}\n")
            for host, slots in active_hosts:
                handle.write(f"  - {host} slots={slots}\n")
            handle.write(f"Transforms: {', '.join(selected)}\n")
            handle.write(f"Kernel dg: {k_dg}\n")
            handle.write(f"Kernel dc: {k_dc}\n")
            handle.write(f"Return code: {result.returncode}\n")
            handle.write("\n-- Task summary --\n")
            if task_lines:
                for line in task_lines:
                    handle.write(line + "\n")
            else:
                handle.write("No task summary lines were emitted.\n")
            handle.write("\n-- Stdout --\n")
            handle.write(result.stdout or "")
            handle.write("\n\n-- Stderr --\n")
            handle.write(result.stderr or "")

    def _update_metrics(self, total_bytes: int, elapsed_seconds: float | None = None):
        """Calcula el rendimiento (B/s) y actualiza los paneles de costo."""
        if elapsed_seconds and elapsed_seconds > 0:
            throughput = total_bytes / elapsed_seconds
            self.last_throughput_bps = throughput
        else:
            throughput = self.last_throughput_bps or 0.0

        self.current_total_bytes = total_bytes
        self.workload_var.set(
            f"{len(self.images)} archivos | {self._format_bytes(total_bytes)}"
        )
        self.bps_var.set(self._scientific_bps(throughput))

        if throughput > 0:
            estimated_seconds = total_bytes / throughput
            self._estimated_total_seconds = estimated_seconds
            self.eta_var.set(f"ETA: {self._format_duration(estimated_seconds)}")
        elif self._estimated_total_seconds:
            self.eta_var.set(f"ETA: {self._format_duration(self._estimated_total_seconds)}")
        else:
            self.eta_var.set("ETA: --:--:--")

        local_cost, aws_cost, diff = self._annual_costs()
        self.cost_local_var.set(f"${local_cost:,.2f} / año")
        self.cost_aws_var.set(f"${aws_cost:,.2f} / año")
        self.cost_diff_var.set(f"${diff:,.2f} / año")

    def _update_progress_tick(self):
        """
        Bucle de animación de la barra de progreso. 
        Utiliza la estimación de tiempo calculada al inicio para simular el avance.
        """
        if not self._run_in_progress or self._run_started_at is None:
            return

        if self._estimated_total_seconds and self._estimated_total_seconds > 0:
            elapsed = time.monotonic() - self._run_started_at
            percent = min(99.0, (elapsed / self._estimated_total_seconds) * 100.0)
            remaining = max(0.0, self._estimated_total_seconds - elapsed)
            self.progress_var.set(percent)
            self.total_var.set(f"{percent:.1f} %")
            self.eta_var.set(f"ETA: {self._format_duration(remaining)}")
        else:
            self.progress_var.set(50.0)
            self.total_var.set("50.0 %")

        self._progress_after_id = self.after(500, self._update_progress_tick)

    def _stop_progress_tick(self):
        """Detiene el bucle de animación."""
        if self._progress_after_id is not None:
            self.after_cancel(self._progress_after_id)
            self._progress_after_id = None

    def _load_images_from_folder(self, folder: str) -> tuple[list[str], str]:
        if not folder:
            return [], "Seleccione una carpeta"

        try:
            candidates = []
            for name in sorted(os.listdir(folder)):
                full_path = os.path.join(folder, name)
                if os.path.isfile(full_path) and name.lower().endswith(".bmp"):
                    candidates.append(full_path)
                if len(candidates) >= self.MAX_INPUT_FILES:
                    break

            if len(candidates) >= self.MAX_INPUT_FILES:
                message = f"Carpeta cargada con {self.MAX_INPUT_FILES} archivos máximos"
            else:
                message = f"Carpeta cargada con {len(candidates)} archivos"
            return candidates, message
        except Exception as exc:
            return [], f"Error al leer carpeta: {exc}"

    def _apply_images(self, paths: list[str], source_label: str):
        """Actualiza el estado interno cuando se cargan nuevas imágenes."""
        self.images = paths[: self.MAX_INPUT_FILES]
        self.listbox.delete(0, "end")
        for path in self.images:
            self.listbox.insert("end", os.path.basename(path))

        total_bytes = self._selected_total_bytes(self.images)
        self.input_var.set(source_label)
        self.workload_var.set(
            f"{len(self.images)} archivos | {self._format_bytes(total_bytes)}"
        )
        self._update_metrics(total_bytes)

    def _select_folder(self):
        folder = filedialog.askdirectory(title="Seleccionar carpeta de entrada")
        if not folder:
            return

        paths, message = self._load_images_from_folder(folder)
        if not paths:
            messagebox.showwarning("Sin archivos BMP", message)
            return

        if len(paths) >= self.MAX_INPUT_FILES:
            messagebox.showinfo(
                "Límite alcanzado",
                f"Solo se cargarán los primeros {self.MAX_INPUT_FILES} archivos BMP.",
            )

        self.input_dir = folder
        self._apply_images(paths, folder)

    def _set_drop_paths(self, raw_paths):
        paths = []
        for path in raw_paths:
            path = path.strip()
            if not path:
                continue

            if os.path.isdir(path):
                folder_paths, _ = self._load_images_from_folder(path)
                for folder_path in folder_paths:
                    if folder_path not in paths:
                        paths.append(folder_path)
                continue

            if path.lower().endswith(".bmp") and os.path.isfile(path):
                if path not in paths:
                    paths.append(path)

        if not paths:
            return

        if len(paths) > self.MAX_INPUT_FILES:
            paths = paths[: self.MAX_INPUT_FILES]
        source_label = os.path.dirname(paths[0]) if paths else ""
        self._apply_images(paths, source_label)

    def _set_run_state(self, running: bool):
        """Bloquea o desbloquea la UI dependiendo de si hay un proceso activo."""
        self._run_in_progress = running
        self.exec_btn.config(
            state="disabled" if running else "normal",
            text="  Procesando...  " if running else "  Ejecutar  ",
        )
        self.select_folder_btn.config(state="disabled" if running else "normal")

    def _build_menu(self):
        menubar = tk.Menu(self)

        app_menu = tk.Menu(menubar, tearoff=0)
        app_menu.add_command(label="Acerca de", command=self._show_about)
        app_menu.add_separator()
        app_menu.add_command(label="Salir", command=self.quit)

        menubar.add_cascade(label="Menu", menu=app_menu)
        self.config(menu=menubar)

    def _show_about(self):
        """Muestra la ventana con los créditos del proyecto."""
        win = tk.Toplevel(self)
        win.title("Acerca de")
        win.configure(bg=BG)
        win.resizable(False, False)

        self.update_idletasks()
        x = self.winfo_x() + self.winfo_width()  + 20
        y = self.winfo_y() + 60
        win.geometry(f"320x340+{x}+{y}")

        frame = tk.Frame(win, bg=BG, padx=24, pady=24)
        frame.pack(fill="both", expand=True)

        info_lines = [
            ("TC3003",                   True),
            ("Tecnológico de Monterrey", False),
            ("Campus Puebla",            False),
            ("Mayo 2026",                False),
        ]

        for text, bold in info_lines:
            font = ("Helvetica", 12, "bold") if bold else ("Helvetica", 12)
            tk.Label(frame, text=text, bg=BG, fg=FG,
                     font=font, anchor="w").pack(fill="x")

        tk.Frame(frame, bg="#444444", height=1).pack(fill="x", pady=12)

        tk.Label(frame, text="Equipo:", bg=BG, fg=FG,
                 font=("Helvetica", 12, "bold"), anchor="w").pack(fill="x")

        integrantes = [
            "1- Alejandro Guzmán",
            "2- Juan Daniel Salmeron",
            "3- Javier Cuatepotzo",
            "4- Anhuar Maldonado",
            "5- Manuel Eduardo Covarrubias",
        ]

        for nombre in integrantes:
            tk.Label(frame, text=nombre, bg=BG, fg=FG,
                     font=("Helvetica", 11), anchor="w").pack(fill="x", pady=1)

        tk.Frame(frame, bg="#444444", height=1).pack(fill="x", pady=12)

        # Logo textual del Tec (esquina inferior derecha)
        logo_frame = tk.Frame(frame, bg=BG)
        logo_frame.pack(fill="x")

        tk.Button(
            logo_frame,
            text="Cerrar",
            bg=BTN, fg=FG,
            activebackground="#666",
            activeforeground=FG,
            relief="flat",
            font=("Helvetica", 11),
            cursor="hand2",
            command=win.destroy,
            padx=10, pady=4,
        ).pack(side="left")

        # Logo del Tec en "Acerca de" — se carga con tamaño más pequeño
        about_logo = self._load_logo(size=50)
        if about_logo:
            logo_lbl = tk.Label(logo_frame, image=about_logo, bg=BG)
            logo_lbl.image = about_logo   # evitar que el GC lo elimine
            logo_lbl.pack(side="right")
        else:
            tk.Label(logo_frame, text="TEC de Monterrey",
                     bg=BG, fg=SUBFG, font=("Helvetica", 9, "bold")
                     ).pack(side="right")

    def _build_ui(self):
        """Ensambla el layout principal del Dashboard."""
        root = tk.Frame(self, bg=BG, padx=18, pady=18)
        root.pack(fill="both", expand=True)

        top = tk.Frame(root, bg=BG)
        top.pack(fill="both", expand=True)

        self._build_drop_panel(top)
        self._build_controls(top)

        tk.Frame(root, bg="#444444", height=1).pack(fill="x", pady=(14, 0))

        self._build_bottom(root)

    def _build_drop_panel(self, parent):
        """Construye la zona de Drop de archivos a la izquierda."""
        frame = tk.Frame(parent, bg=PANEL, width=265, height=310)
        frame.pack(side="left", fill="y", padx=(0, 18))
        frame.pack_propagate(False)

        hint = tk.Label(
            frame,
            text="Arrastra imágenes\no una carpeta\nmáximo 600\n.bmp",
            bg=PANEL, fg=SUBFG,
            font=("Helvetica", 12),
            justify="center",
        )
        hint.pack(pady=(16, 6))

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

        tk.Label(
            frame,
            text="Clic derecho para quitar archivo",
            bg=PANEL, fg=SUBFG,
            font=("Helvetica", 8),
        ).pack(pady=(0, 6))

        self.listbox.bind("<Button-2>", self._remove_file)   # macOS
        self.listbox.bind("<Button-3>", self._remove_file)   # Windows/Linux

        for widget in (frame, hint, self.listbox):
            widget.drop_target_register(DND_FILES)
            widget.dnd_bind("<<Drop>>", self._on_drop)

    def _build_controls(self, parent):
        """Construye los selectores de transformaciones y kernels."""
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
                cursor="hand2",
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

        spacer = tk.Frame(frame, bg=BG, height=10)
        spacer.pack()

        todas_row = tk.Frame(frame, bg=BG)
        todas_row.pack(fill="x")

        tk.Button(
            todas_row,
            text="  Todas  ",
            bg=BTN, fg=FG,
            activebackground="#666",
            activeforeground=FG,
            relief="flat",
            font=("Helvetica", 12),
            cursor="hand2",
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

        folder_row = tk.Frame(frame, bg=BG)
        folder_row.pack(fill="x", pady=(18, 0))

        self.select_folder_btn = tk.Button(
            folder_row,
            text="  Seleccionar carpeta  ",
            bg=BTN, fg=FG,
            activebackground="#666",
            activeforeground=FG,
            relief="flat",
            font=("Helvetica", 12),
            cursor="hand2",
            command=self._select_folder,
            padx=6,
            pady=4,
        )
        self.select_folder_btn.pack(side="left")

        tk.Label(
            folder_row,
            text="Hasta 600 archivos .bmp",
            bg=BG,
            fg=SUBFG,
            font=("Helvetica", 10),
        ).pack(side="left", padx=(12, 0))

    def _build_bottom(self, parent):
        """Construye el panel de métricas, costos y el botón Ejecutar."""
        bottom = tk.Frame(parent, bg=BG)
        bottom.pack(fill="x", pady=(14, 0))

        info = tk.Frame(bottom, bg=BG)
        info.pack(side="left", fill="both", expand=True)

        top_info = tk.Frame(info, bg=BG)
        top_info.pack(fill="x")

        left_info = tk.Frame(top_info, bg=BG)
        left_info.pack(side="left", fill="both", expand=True)
        tk.Label(left_info, text="Carpeta de entrada",
                 bg=BG, fg=FG, font=("Helvetica", 11)).pack(anchor="w")
        tk.Entry(
            left_info,
            textvariable=self.input_var,
            width=40,
            bg=FIELD, fg=FG,
            relief="flat",
            font=("Helvetica", 10),
            state="readonly",
            readonlybackground=FIELD,
        ).pack(anchor="w", pady=(3, 8), fill="x")

        right_info = tk.Frame(top_info, bg=BG)
        right_info.pack(side="left", fill="both", expand=True, padx=(10, 0))
        tk.Label(right_info, text="Carpeta de salida",
                 bg=BG, fg=FG, font=("Helvetica", 11)).pack(anchor="w")
        tk.Entry(
            right_info,
            textvariable=self.output_var,
            width=40,
            bg=FIELD, fg=FG,
            relief="flat",
            font=("Helvetica", 10),
            state="readonly",
            readonlybackground=FIELD,
        ).pack(anchor="w", pady=(3, 8), fill="x")

        second_info = tk.Frame(info, bg=BG)
        second_info.pack(fill="x")

        left_second = tk.Frame(second_info, bg=BG)
        left_second.pack(side="left", fill="both", expand=True)
        tk.Label(left_second, text="Tiempo de ejecución",
                 bg=BG, fg=FG, font=("Helvetica", 11)).pack(anchor="w")
        tk.Entry(
            left_second,
            textvariable=self.tiempo_var,
            width=40,
            bg=FIELD, fg=FG,
            relief="flat",
            font=("Helvetica", 10),
            state="readonly",
            readonlybackground=FIELD,
        ).pack(anchor="w", pady=(3, 8), fill="x")

        right_second = tk.Frame(second_info, bg=BG)
        right_second.pack(side="left", fill="both", expand=True, padx=(10, 0))
        tk.Label(right_second, text="Carga total",
                 bg=BG, fg=FG, font=("Helvetica", 11)).pack(anchor="w")
        tk.Entry(
            right_second,
            textvariable=self.workload_var,
            width=40,
            bg=FIELD, fg=FG,
            relief="flat",
            font=("Helvetica", 10),
            state="readonly",
            readonlybackground=FIELD,
        ).pack(anchor="w", pady=(3, 8), fill="x")

        third_info = tk.Frame(info, bg=BG)
        third_info.pack(fill="x")

        left_third = tk.Frame(third_info, bg=BG)
        left_third.pack(side="left", fill="both", expand=True)
        tk.Label(left_third, text="Rendimiento (bytes/s)",
                 bg=BG, fg=FG, font=("Helvetica", 11)).pack(anchor="w")
        tk.Entry(
            left_third,
            textvariable=self.bps_var,
            width=40,
            bg=FIELD, fg=FG,
            relief="flat",
            font=("Helvetica", 10),
            state="readonly",
            readonlybackground=FIELD,
        ).pack(anchor="w", pady=(3, 8), fill="x")

        right_third = tk.Frame(third_info, bg=BG)
        right_third.pack(side="left", fill="both", expand=True, padx=(10, 0))
        tk.Label(right_third, text="Estimación restante",
                 bg=BG, fg=FG, font=("Helvetica", 11)).pack(anchor="w")
        ttk.Progressbar(
            right_third,
            variable=self.progress_var,
            maximum=100.0,
            mode="determinate",
            length=240,
        ).pack(anchor="w", pady=(6, 3), fill="x")
        tk.Label(
            right_third,
            textvariable=self.total_var,
            bg=BG,
            fg=SUBFG,
            font=("Helvetica", 10),
        ).pack(anchor="w")

        fourth_info = tk.Frame(info, bg=BG)
        fourth_info.pack(fill="x")

        left_fourth = tk.Frame(fourth_info, bg=BG)
        left_fourth.pack(side="left", fill="both", expand=True)
        tk.Label(left_fourth, text="Costo anual local",
                 bg=BG, fg=FG, font=("Helvetica", 11)).pack(anchor="w")
        tk.Entry(
            left_fourth,
            textvariable=self.cost_local_var,
            width=40,
            bg=FIELD, fg=FG,
            relief="flat",
            font=("Helvetica", 10),
            state="readonly",
            readonlybackground=FIELD,
        ).pack(anchor="w", pady=(3, 8), fill="x")

        right_fourth = tk.Frame(fourth_info, bg=BG)
        right_fourth.pack(side="left", fill="both", expand=True, padx=(10, 0))
        tk.Label(right_fourth, text="Costo anual AWS",
                 bg=BG, fg=FG, font=("Helvetica", 11)).pack(anchor="w")
        tk.Entry(
            right_fourth,
            textvariable=self.cost_aws_var,
            width=40,
            bg=FIELD, fg=FG,
            relief="flat",
            font=("Helvetica", 10),
            state="readonly",
            readonlybackground=FIELD,
        ).pack(anchor="w", pady=(3, 8), fill="x")

        fifth_info = tk.Frame(info, bg=BG)
        fifth_info.pack(fill="x")

        left_fifth = tk.Frame(fifth_info, bg=BG)
        left_fifth.pack(side="left", fill="both", expand=True)
        tk.Label(left_fifth, text="Diferencia anual",
                 bg=BG, fg=FG, font=("Helvetica", 11)).pack(anchor="w")
        tk.Entry(
            left_fifth,
            textvariable=self.cost_diff_var,
            width=40,
            bg=FIELD, fg=FG,
            relief="flat",
            font=("Helvetica", 10),
            state="readonly",
            readonlybackground=FIELD,
        ).pack(anchor="w", pady=(3, 8), fill="x")

        right = tk.Frame(bottom, bg=BG)
        right.pack(side="right", anchor="n", padx=(12, 0))

        self.exec_btn = tk.Button(
            right,
            text="  Ejecutar  ",
            bg=BTN, fg=FG,
            activebackground="#666",
            activeforeground=FG,
            relief="flat",
            font=("Helvetica", 14),
            cursor="hand2",
            command=self._execute,
            padx=12, pady=6,
        )
        self.exec_btn.pack(pady=(0, 10))

        cluster_box = tk.Frame(right, bg=PANEL, width=220, height=160)
        cluster_box.pack(fill="both", expand=False, pady=(0, 10))
        cluster_box.pack_propagate(False)

        tk.Label(
            cluster_box,
            text="Computadoras del cluster",
            bg=PANEL,
            fg=FG,
            font=("Helvetica", 10, "bold"),
        ).pack(anchor="w", padx=8, pady=(8, 2))
        tk.Label(
            cluster_box,
            textvariable=self.cluster_var,
            bg=PANEL,
            fg=SUBFG,
            font=("Helvetica", 8),
            wraplength=200,
            justify="left",
        ).pack(anchor="w", padx=8)

        self.cluster_listbox = tk.Listbox(
            cluster_box,
            bg=PANEL,
            fg=FG,
            selectbackground=ACCENT,
            selectforeground=FG,
            font=("Helvetica", 9),
            relief="flat",
            bd=0,
            activestyle="none",
            height=6,
        )
        self.cluster_listbox.pack(fill="both", expand=True, padx=8, pady=(6, 8))

        if self.logo_img:
            tk.Label(right, image=self.logo_img, bg=BG).pack()
        else:
            tk.Label(right, text="TEC\nde Monterrey",
                     bg=BG, fg=SUBFG, font=("Helvetica", 8, "bold"),
                     justify="center").pack()

    def _on_drop(self, event):
        self._set_drop_paths(raw_paths)

    def _remove_file(self, event):
        sel = self.listbox.curselection()
        if sel:
            idx = sel[0]
            self.listbox.delete(idx)
            self.images.pop(idx)
            total_bytes = self._selected_total_bytes(self.images)
            self._update_metrics(total_bytes)

    def _select_all(self):
        for var in (self.var_vg, self.var_vc, self.var_hg,
                    self.var_hc, self.var_dg, self.var_dc):
            var.set(True)

    def _execute(self):
        """Valida entradas e inicia el hilo de ejecución de MPI."""
        selected = []
        if self.var_vg.get(): selected.append("vg")
        if self.var_vc.get(): selected.append("vc")
        if self.var_hg.get(): selected.append("hg")
        if self.var_hc.get(): selected.append("hc")
        if self.var_dg.get(): selected.append("dg")
        if self.var_dc.get(): selected.append("dc")

        if not selected:
            self.tiempo_var.set("⚠ Selecciona al menos una transformación")
            return

        try:
            k_dg = max(1, int(self.kernel_dg.get()))
        except ValueError:
            k_dg = 16
        try:
            k_dc = max(1, int(self.kernel_dc.get()))
        except ValueError:
            k_dc = 16

        if not self.images:
            self.tiempo_var.set("⚠ Selecciona una carpeta o arrastra archivos .bmp")
            return

        #  Inicializa la estimación de tiempo para la barra de progreso 
        total_bytes = self._selected_total_bytes(self.images)
        self._update_metrics(total_bytes)
        if self.last_throughput_bps and self.last_throughput_bps > 0:
            self._estimated_total_seconds = total_bytes / self.last_throughput_bps
        elif total_bytes > 0:
            self._estimated_total_seconds = max(1.0, total_bytes / (8.0 * 1024.0 * 1024.0))
        else:
            self._estimated_total_seconds = None

        self._set_run_state(True)
        self.tiempo_var.set("Procesando…")
        self.total_var.set("0.0 %")
        self.progress_var.set(0.0)
        self._run_started_at = time.monotonic()
        self._stop_progress_tick()
        self._update_progress_tick()

        threading.Thread(
            target=self._run,
            args=(selected, k_dg, k_dc),
            daemon=True,
        ).start()

    def _run(self, selected: list, k_dg: int, k_dc: int):
        """
        Método ejecutado en el hilo secundario. 
        Prepara el clúster, lanza mpirun y captura la salida.
        """
        script_dir = os.path.dirname(os.path.abspath(__file__))
        executable = os.path.join(script_dir, "execute.sh")
        img_dir    = self.output_dir
        machinefile = os.path.join(script_dir, "machinefile")
        os.makedirs(img_dir, exist_ok=True)

        launcher = shutil.which("mpirun") or shutil.which("mpiexec")
        if not launcher:
            self.after(0, self._on_error,
                       "No se encontró mpirun/mpiexec en el sistema")
            return

        try:
            active_machinefile, active_hosts = self._prepare_machinefile(script_dir)
        except Exception as exc:
            self.after(0, self._on_error, f"No se pudo preparar machinefile: {exc}")
            return

        self.active_machinefile = active_machinefile
        self.after(0, self._set_cluster_hosts, active_hosts, "Hosts activos")

        nprocs = 1
        if os.path.exists(active_machinefile):
            nprocs = self._machinefile_slots(active_machinefile)

        env_nprocs = os.environ.get("MPI_NP") or os.environ.get("MPI_PROCS")
        if env_nprocs:
            try:
                nprocs = max(1, int(env_nprocs))
            except ValueError:
                pass

        cmd = [
            launcher,
            "--prtemca",
            "oob_tcp_if_include",
            self.MPI_OOB_IF_INCLUDE,
            "--mca",
            "btl_tcp_if_include",
            self.MPI_BTL_IF_INCLUDE,
            "--mca",
            "btl_tcp_disable_family",
            self.MPI_BTL_DISABLE_FAMILY,
        ]
        if os.path.exists(active_machinefile):
            cmd += ["--hostfile", active_machinefile]
        cmd += [executable] + self.images + ["--transforms"] + selected

        if "dg" in selected:
            cmd += ["--kernel-dg", str(k_dg)]
        if "dc" in selected:
            cmd += ["--kernel-dc", str(k_dc)]

        print(cmd)
        started_at = time.time()
        try:
            result = subprocess.run(
                cmd,
                cwd=script_dir,
                capture_output=True,
                text=True,
            )
            finished_at = time.time()

            log_path = ""
            for line in result.stdout.splitlines():
                if line.startswith("LOG_FILE:"):
                    rel_log_path = line.split(":", 1)[1].strip()
                    log_path = os.path.abspath(os.path.join(script_dir, rel_log_path))
                    break

            if not log_path:
                log_path = os.path.join(
                    script_dir,
                    "logs",
                    time.strftime("run_%Y%m%d_%H%M%S.log", time.localtime(finished_at)),
                )
            self.last_log_path = log_path

            tiempo = "—"
            for line in result.stdout.splitlines():
                if line.startswith("TIEMPO:"):
                    t = float(line.split(":", 1)[1])
                    tiempo = f"{t:.4f} s"
                    if t > 0 and self.current_total_bytes > 0:
                        self.last_throughput_bps = self.current_total_bytes / t

            if result.returncode != 0:
                self.after(0, self._on_error,
                           f"mpirun terminó con código {result.returncode}. Revisa {log_path}")
                return

            self.after(0, self._on_done, tiempo, img_dir, log_path)

        except FileNotFoundError:
            self.after(0, self._on_error,
                       f"Ejecutable no encontrado: {executable}\n"
                       "Compila: mpicc -O2 main_pararell.c -o imgprocP\n"
                       "y asegúrate de que execute.sh sea ejecutable")
        except Exception as e:
            self.after(0, self._on_error, str(e))

    def _on_done(self, tiempo: str, img_dir: str, log_path: str):
        self._stop_progress_tick()
        self._run_in_progress = False
        self.tiempo_var.set(tiempo)
        self.output_dir = img_dir
        self.output_var.set(img_dir)
        self.last_log_path = log_path
        self.progress_var.set(100.0)
        self.total_var.set("100.0 %")
        if self.current_total_bytes > 0 and tiempo.endswith(" s"):
            try:
                elapsed = float(tiempo.split()[0])
                self._update_metrics(self.current_total_bytes, elapsed)
            except ValueError:
                self._update_metrics(self.current_total_bytes)
        self._set_run_state(False)

    def _on_error(self, msg: str):
        self._stop_progress_tick()
        self._run_in_progress = False
        self.tiempo_var.set(f"Error: {msg}")
        self._set_run_state(False)

if __name__ == "__main__":
    app = App()
    app.mainloop()