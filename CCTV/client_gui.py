import socket
import tkinter as tk
from tkinter import ttk, messagebox
import threading
import struct
import math
import base64


def send_command(host: str, port: int, command: str) -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(3.0)
        s.connect((host, port))
        s.sendall(command.encode("utf-8"))
        data = s.recv(4096)
    return data.decode("utf-8", errors="replace").strip()


class ClientGui(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("Depth TRT Client")
        self.geometry("360x240")
        self.resizable(False, False)

        self.host_var = tk.StringVar(value="127.0.0.1")
        self.port_var = tk.StringVar(value="9090")
        self.channel_var = tk.StringVar(value="0")
        self.mode_var = tk.StringVar(value="headless")
        self.rx_var = tk.StringVar(value="-20")
        self.ry_var = tk.StringVar(value="35")

        frm = ttk.Frame(self, padding=12)
        frm.pack(fill=tk.BOTH, expand=True)

        ttk.Label(frm, text="Server Host").grid(row=0, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.host_var, width=18).grid(row=0, column=1, sticky="w")

        ttk.Label(frm, text="Server Port").grid(row=1, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.port_var, width=10).grid(row=1, column=1, sticky="w")

        ttk.Label(frm, text="Channel").grid(row=2, column=0, sticky="w")
        channel_box = ttk.Combobox(frm, textvariable=self.channel_var, width=8, state="readonly")
        channel_box["values"] = ("0", "1", "2", "3")
        channel_box.grid(row=2, column=1, sticky="w")

        ttk.Label(frm, text="Run Mode").grid(row=3, column=0, sticky="w")
        ttk.Radiobutton(frm, text="Headless", variable=self.mode_var, value="headless").grid(row=3, column=1, sticky="w")
        ttk.Radiobutton(frm, text="GUI", variable=self.mode_var, value="gui").grid(row=4, column=1, sticky="w")

        btn_frame = ttk.Frame(frm)
        btn_frame.grid(row=5, column=0, columnspan=2, pady=12, sticky="w")

        ttk.Button(btn_frame, text="Start", command=self.on_start).grid(row=0, column=0, padx=(0, 8))
        ttk.Button(btn_frame, text="Stop", command=self.on_stop).grid(row=0, column=1)
        ttk.Button(btn_frame, text="View Depth", command=self.on_view_depth).grid(row=0, column=2, padx=(8, 0))
        ttk.Button(btn_frame, text="View PC", command=self.on_view_pc).grid(row=0, column=3, padx=(8, 0))

        self.status = tk.StringVar(value="Waiting for server response...")
        ttk.Label(frm, textvariable=self.status, foreground="#1f5e9a").grid(row=6, column=0, columnspan=2, sticky="w")

        ttk.Label(frm, text="View RotX").grid(row=7, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.rx_var, width=8).grid(row=7, column=1, sticky="w")
        ttk.Label(frm, text="View RotY").grid(row=8, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.ry_var, width=8).grid(row=8, column=1, sticky="w")
        ttk.Button(frm, text="Apply View", command=self.on_apply_view).grid(row=9, column=0, columnspan=2, pady=8, sticky="w")

        self._stream_stop = threading.Event()
        self._stream_thread = None

    def on_start(self) -> None:
        host = self.host_var.get().strip()
        port = int(self.port_var.get().strip())
        channel = self.channel_var.get().strip()
        mode = self.mode_var.get().strip()

        command = f"channel={channel} {mode}"
        try:
            resp = send_command(host, port, command)
            self.status.set(resp or "OK")
        except Exception as e:
            messagebox.showerror("Connection Error", str(e))

    def on_stop(self) -> None:
        host = self.host_var.get().strip()
        port = int(self.port_var.get().strip())
        try:
            resp = send_command(host, port, "stop")
            self.status.set(resp or "OK")
        except Exception as e:
            messagebox.showerror("Connection Error", str(e))

    def on_apply_view(self) -> None:
        host = self.host_var.get().strip()
        port = int(self.port_var.get().strip())
        rx = self.rx_var.get().strip()
        ry = self.ry_var.get().strip()
        try:
            cmd = f"pc_view rx={rx} ry={ry}"
            resp = send_command(host, port, cmd)
            self.status.set(resp or "OK")
        except Exception as e:
            messagebox.showerror("Connection Error", str(e))

    def on_view_depth(self) -> None:
        if self._stream_thread and self._stream_thread.is_alive():
            messagebox.showinfo("Depth Viewer", "Depth viewer is already running.")
            return

        host = self.host_var.get().strip()
        port = int(self.port_var.get().strip())

        win = tk.Toplevel(self)
        win.title("Depth 3D View (Client)")
        width, height = 640, 480
        canvas = tk.Canvas(win, width=width, height=height, bg="black")
        canvas.pack()

        def on_close():
            self._stream_stop.set()
            win.destroy()

        win.protocol("WM_DELETE_WINDOW", on_close)
        self._stream_stop.clear()

        def recv_exact(sock, n):
            buf = bytearray()
            while len(buf) < n:
                chunk = sock.recv(n - len(buf))
                if not chunk:
                    raise ConnectionError("Socket closed")
                buf.extend(chunk)
            return bytes(buf)

        def stream_loop():
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                    s.settimeout(5.0)
                    s.connect((host, port))
                    s.sendall(b"depth_stream\n")
                    # Read one line response
                    line = b""
                    while not line.endswith(b"\n"):
                        line += s.recv(1)
                    if not line.startswith(b"OK depth_stream"):
                        raise RuntimeError(f"Unexpected response: {line.decode(errors='ignore')}")
                    s.settimeout(None)

                    while not self._stream_stop.is_set():
                        header = recv_exact(s, 16)
                        frame_idx, w, h, payload = struct.unpack("<IIII", header)
                        data = recv_exact(s, payload)

                        stride = 4
                        # Intrinsics from wide FOV, scaled to depth size.
                        hfov = math.radians(109.0)
                        vfov = math.radians(55.0)
                        fx = (w * 0.5) / math.tan(hfov * 0.5)
                        fy = (h * 0.5) / math.tan(vfov * 0.5)
                        cx = w * 0.5
                        cy = h * 0.5

                        points = []
                        min_x = min_y = min_z = None
                        max_x = max_y = max_z = None

                        # Sample points
                        for y in range(0, h, stride):
                            row = y * w
                            for x in range(0, w, stride):
                                idx = (row + x) * 4
                                z = struct.unpack_from("<f", data, idx)[0]
                                if z <= 0.1 or z > 80.0:
                                    continue
                                X = (x - cx) * z / fx
                                Y = (y - cy) * z / fy
                                points.append((X, Y, z))
                                if min_x is None:
                                    min_x = max_x = X
                                    min_y = max_y = Y
                                    min_z = max_z = z
                                else:
                                    min_x = min(min_x, X)
                                    max_x = max(max_x, X)
                                    min_y = min(min_y, Y)
                                    max_y = max(max_y, Y)
                                    min_z = min(min_z, z)
                                    max_z = max(max_z, z)

                        if not points:
                            continue

                        rx = math.radians(-20.0)
                        ry = math.radians(35.0)
                        cos_y = math.cos(ry)
                        sin_y = math.sin(ry)
                        cos_x = math.cos(rx)
                        sin_x = math.sin(rx)

                        rotated = []
                        min_rx = min_ry = None
                        max_rx = max_ry = None
                        min_rz = max_rz = None
                        for (X, Y, Z) in points:
                            x1 = X * cos_y + Z * sin_y
                            z1 = -X * sin_y + Z * cos_y
                            y1 = Y * cos_x - z1 * sin_x
                            z2 = Y * sin_x + z1 * cos_x
                            rotated.append((x1, y1, z2))
                            if min_rx is None:
                                min_rx = max_rx = x1
                                min_ry = max_ry = y1
                                min_rz = max_rz = z2
                            else:
                                min_rx = min(min_rx, x1)
                                max_rx = max(max_rx, x1)
                                min_ry = min(min_ry, y1)
                                max_ry = max(max_ry, y1)
                                min_rz = min(min_rz, z2)
                                max_rz = max(max_rz, z2)

                        pad = 10.0
                        scale_x = (width - 2 * pad) / (max_rx - min_rx + 1e-6)
                        scale_y = (height - 2 * pad) / (max_ry - min_ry + 1e-6)
                        scale = min(scale_x, scale_y)

                        def depth_color(z):
                            t = (z - min_rz) / (max_rz - min_rz + 1e-6)
                            t = max(0.0, min(1.0, t))
                            r = int(255 * min(1.0, max(0.0, 2.0 * (t - 0.5))))
                            g = int(255 * min(1.0, max(0.0, 2.0 * (0.5 - abs(t - 0.5)))))
                            b = int(255 * min(1.0, max(0.0, 1.0 - 2.0 * t)))
                            return f"#{r:02x}{g:02x}{b:02x}"

                        canvas.delete("all")
                        for (x1, y1, z2) in rotated:
                            px = (x1 - min_rx) * scale + pad
                            py = height - ((y1 - min_ry) * scale + pad)
                            if px < 0 or px >= width or py < 0 or py >= height:
                                continue
                            canvas.create_rectangle(px, py, px + 1, py + 1, outline=depth_color(z2), fill=depth_color(z2))

            except Exception as e:
                if not self._stream_stop.is_set():
                    messagebox.showerror("Depth Stream Error", str(e))

        self._stream_thread = threading.Thread(target=stream_loop, daemon=True)
        self._stream_thread.start()

    def on_view_pc(self) -> None:
        if self._stream_thread and self._stream_thread.is_alive():
            messagebox.showinfo("PC Viewer", "A stream viewer is already running.")
            return

        host = self.host_var.get().strip()
        port = int(self.port_var.get().strip())

        win = tk.Toplevel(self)
        win.title("PointCloud View (Server Rendered)")
        width, height = 640, 480
        canvas = tk.Canvas(win, width=width, height=height, bg="black")
        canvas.pack()

        def on_close():
            self._stream_stop.set()
            win.destroy()

        win.protocol("WM_DELETE_WINDOW", on_close)
        self._stream_stop.clear()

        def recv_exact(sock, n):
            buf = bytearray()
            while len(buf) < n:
                chunk = sock.recv(n - len(buf))
                if not chunk:
                    raise ConnectionError("Socket closed")
                buf.extend(chunk)
            return bytes(buf)

        def stream_loop():
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                    s.settimeout(5.0)
                    s.connect((host, port))
                    s.sendall(b"pc_stream\n")
                    line = b""
                    while not line.endswith(b"\n"):
                        line += s.recv(1)
                    if not line.startswith(b"OK pc_stream"):
                        raise RuntimeError(f"Unexpected response: {line.decode(errors='ignore')}")
                    s.settimeout(None)

                    img = None
                    while not self._stream_stop.is_set():
                        header = recv_exact(s, 16)
                        frame_idx, w, h, payload = struct.unpack("<IIII", header)
                        data = recv_exact(s, payload)
                        b64 = base64.b64encode(data)
                        img = tk.PhotoImage(data=b64)
                        canvas.delete("all")
                        canvas.create_image(0, 0, image=img, anchor="nw")
                        canvas.image = img
            except Exception as e:
                if not self._stream_stop.is_set():
                    messagebox.showerror("PC Stream Error", str(e))

        self._stream_thread = threading.Thread(target=stream_loop, daemon=True)
        self._stream_thread.start()


if __name__ == "__main__":
    app = ClientGui()
    app.mainloop()
