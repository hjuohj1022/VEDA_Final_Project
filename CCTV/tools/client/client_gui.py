import socket
import ssl
from pathlib import Path
import tkinter as tk
from tkinter import ttk, messagebox
import threading
import struct
import base64
import time
import math

CONTROL_CONNECT_TIMEOUT_SEC = 8.0
STREAM_CONNECT_TIMEOUT_SEC = 8.0


def _resolve_file_path(p: str) -> str:
    raw = Path(p)
    if raw.is_absolute() and raw.exists():
        return str(raw)

    cwd_candidate = (Path.cwd() / raw).resolve()
    if cwd_candidate.exists():
        return str(cwd_candidate)

    project_root = Path(__file__).resolve().parents[2]
    repo_candidate = (project_root / raw).resolve()
    if repo_candidate.exists():
        return str(repo_candidate)

    return str(raw)


def _require_existing_file(label: str, p: str) -> str:
    resolved = _resolve_file_path(p)
    if not Path(resolved).exists():
        raise FileNotFoundError(f"{label} file not found: {p} (resolved: {resolved})")
    return resolved


def _socket_family_name(family: int) -> str:
    if family == socket.AF_INET:
        return "AF_INET"
    if family == socket.AF_INET6:
        return "AF_INET6"
    return str(family)


def connect_socket(host: str, port: int, timeout_sec: float,
                   use_mtls: bool, ca_file: str, client_cert: str, client_key: str):
    if not host:
        raise ValueError("Server host is empty")

    tls_context = None
    if use_mtls:
        ca_path = _require_existing_file("CA", ca_file)
        cert_path = _require_existing_file("Client certificate", client_cert)
        key_path = _require_existing_file("Client key", client_key)
        tls_context = ssl.create_default_context(ssl.Purpose.SERVER_AUTH, cafile=ca_path)
        tls_context.check_hostname = False
        tls_context.load_cert_chain(certfile=cert_path, keyfile=key_path)

    try:
        addr_infos = socket.getaddrinfo(host, port, type=socket.SOCK_STREAM, proto=socket.IPPROTO_TCP)
    except socket.gaierror as exc:
        raise ConnectionError(f"Failed to resolve host {host}:{port}: {exc}") from exc

    if not addr_infos:
        raise ConnectionError(f"No TCP address candidates found for {host}:{port}")

    errors = []
    for family, socktype, proto, _canonname, sockaddr in addr_infos:
        sock = None
        tls_sock = None
        try:
            sock = socket.socket(family, socktype, proto)
            sock.settimeout(timeout_sec)
            sock.connect(sockaddr)
            if tls_context is None:
                return sock

            tls_sock = tls_context.wrap_socket(sock, server_hostname=host)
            tls_sock.settimeout(timeout_sec)
            return tls_sock
        except Exception as exc:
            errors.append(f"{_socket_family_name(family)} {sockaddr}: {exc}")
            if tls_sock is not None:
                tls_sock.close()
            elif sock is not None:
                sock.close()

    joined_errors = "; ".join(errors)
    prefix = "mTLS" if use_mtls else "TCP"
    raise ConnectionError(
        f"{prefix} connection to {host}:{port} failed after trying {len(addr_infos)} address(es): {joined_errors}"
    )


def send_command(host: str, port: int, command: str,
                 use_mtls: bool, ca_file: str, client_cert: str, client_key: str) -> str:
    payload = command.rstrip("\r\n") + "\n"
    with connect_socket(host, port, CONTROL_CONNECT_TIMEOUT_SEC, use_mtls, ca_file, client_cert, client_key) as s:
        s.sendall(payload.encode("utf-8"))
        data = s.recv(4096)
    return data.decode("utf-8", errors="replace").strip()


class ClientGui(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("Depth TRT Client")
        self.geometry("760x520")
        self.resizable(False, False)

        self.host_var = tk.StringVar(value="127.0.0.1")
        self.port_var = tk.StringVar(value="9090")
        self.use_mtls_var = tk.BooleanVar(value=True)
        self.ca_file_var = tk.StringVar(value="certs/rootCA.crt")
        self.client_cert_var = tk.StringVar(value="certs/cctv.crt")
        self.client_key_var = tk.StringVar(value="certs/cctv.key")
        self.channel_var = tk.StringVar(value="0")
        self.mode_var = tk.StringVar(value="headless")
        self.rx_var = tk.StringVar(value="-20")
        self.ry_var = tk.StringVar(value="35")
        self.flipx_var = tk.BooleanVar(value=False)
        self.flipy_var = tk.BooleanVar(value=False)
        self.flipz_var = tk.BooleanVar(value=False)
        self.wire_var = tk.BooleanVar(value=False)
        self.mesh_var = tk.BooleanVar(value=False)

        frm = ttk.Frame(self, padding=12)
        frm.pack(fill=tk.BOTH, expand=True)

        ttk.Label(frm, text="Server Host").grid(row=0, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.host_var, width=18).grid(row=0, column=1, sticky="w")

        ttk.Label(frm, text="Server Port").grid(row=1, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.port_var, width=10).grid(row=1, column=1, sticky="w")
        ttk.Checkbutton(frm, text="mTLS", variable=self.use_mtls_var).grid(row=1, column=2, sticky="w")

        ttk.Label(frm, text="CA").grid(row=2, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.ca_file_var, width=30).grid(row=2, column=1, columnspan=2, sticky="w")
        ttk.Label(frm, text="Client Cert").grid(row=3, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.client_cert_var, width=30).grid(row=3, column=1, columnspan=2, sticky="w")
        ttk.Label(frm, text="Client Key").grid(row=4, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.client_key_var, width=30).grid(row=4, column=1, columnspan=2, sticky="w")

        ttk.Label(frm, text="Channel").grid(row=5, column=0, sticky="w")
        channel_box = ttk.Combobox(frm, textvariable=self.channel_var, width=8, state="readonly")
        channel_box["values"] = ("0", "1", "2", "3")
        channel_box.grid(row=5, column=1, sticky="w")

        ttk.Label(frm, text="Run Mode").grid(row=6, column=0, sticky="w")
        ttk.Radiobutton(frm, text="Headless", variable=self.mode_var, value="headless").grid(row=6, column=1, sticky="w")
        ttk.Radiobutton(frm, text="GUI", variable=self.mode_var, value="gui").grid(row=7, column=1, sticky="w")

        btn_frame = ttk.Frame(frm)
        btn_frame.grid(row=8, column=0, columnspan=3, pady=12, sticky="w")

        ttk.Button(btn_frame, text="Start", command=self.on_start).grid(row=0, column=0, padx=(0, 8))
        ttk.Button(btn_frame, text="Stop", command=self.on_stop).grid(row=0, column=1)
        ttk.Button(btn_frame, text="Pause", command=self.on_pause).grid(row=0, column=2, padx=(8, 0))
        ttk.Button(btn_frame, text="Resume", command=self.on_resume).grid(row=0, column=3, padx=(8, 0))
        ttk.Button(btn_frame, text="View PC (Server)", command=self.on_view_pc).grid(row=0, column=4, padx=(8, 0))
        ttk.Button(btn_frame, text="View PC (Client)", command=self.on_view_pc_client).grid(row=0, column=5, padx=(8, 0))

        self.status = tk.StringVar(value="Waiting for server response...")
        ttk.Label(frm, textvariable=self.status, foreground="#1f5e9a").grid(row=9, column=0, columnspan=3, sticky="w")

        ttk.Label(frm, text="View RotX").grid(row=10, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.rx_var, width=8).grid(row=10, column=1, sticky="w")
        ttk.Label(frm, text="View RotY").grid(row=11, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.ry_var, width=8).grid(row=11, column=1, sticky="w")
        ttk.Checkbutton(frm, text="Flip X", variable=self.flipx_var).grid(row=12, column=0, sticky="w")
        ttk.Checkbutton(frm, text="Flip Y", variable=self.flipy_var).grid(row=12, column=1, sticky="w")
        ttk.Checkbutton(frm, text="Flip Z", variable=self.flipz_var).grid(row=13, column=0, sticky="w")
        ttk.Checkbutton(frm, text="Wireframe", variable=self.wire_var).grid(row=13, column=1, sticky="w")
        ttk.Checkbutton(frm, text="Mesh Fill", variable=self.mesh_var).grid(row=14, column=0, sticky="w")
        ttk.Button(frm, text="Apply View", command=self.on_apply_view).grid(row=14, column=1, pady=8, sticky="e")

        self._stream_stop = threading.Event()
        self._stream_thread = None
        self._dragging = False
        self._last_mouse_x = 0
        self._last_mouse_y = 0
        self._last_drag_send_ts = 0.0

    def on_start(self) -> None:
        host = self.host_var.get().strip()
        port = int(self.port_var.get().strip())
        channel = self.channel_var.get().strip()
        mode = self.mode_var.get().strip()

        command = f"channel={channel} {mode}"
        try:
            resp = send_command(host, port, command,
                                self.use_mtls_var.get(),
                                self.ca_file_var.get().strip(),
                                self.client_cert_var.get().strip(),
                                self.client_key_var.get().strip())
            self.status.set(resp or "OK")
        except Exception as e:
            messagebox.showerror("Connection Error", str(e))

    def on_stop(self) -> None:
        host = self.host_var.get().strip()
        port = int(self.port_var.get().strip())
        try:
            resp = send_command(host, port, "stop",
                                self.use_mtls_var.get(),
                                self.ca_file_var.get().strip(),
                                self.client_cert_var.get().strip(),
                                self.client_key_var.get().strip())
            self.status.set(resp or "OK")
        except Exception as e:
            messagebox.showerror("Connection Error", str(e))

    def on_pause(self) -> None:
        host = self.host_var.get().strip()
        port = int(self.port_var.get().strip())
        try:
            resp = send_command(host, port, "pause",
                                self.use_mtls_var.get(),
                                self.ca_file_var.get().strip(),
                                self.client_cert_var.get().strip(),
                                self.client_key_var.get().strip())
            self.status.set(resp or "OK")
        except Exception as e:
            messagebox.showerror("Connection Error", str(e))

    def on_resume(self) -> None:
        host = self.host_var.get().strip()
        port = int(self.port_var.get().strip())
        try:
            resp = send_command(host, port, "resume",
                                self.use_mtls_var.get(),
                                self.ca_file_var.get().strip(),
                                self.client_cert_var.get().strip(),
                                self.client_key_var.get().strip())
            self.status.set(resp or "OK")
        except Exception as e:
            messagebox.showerror("Connection Error", str(e))

    def on_apply_view(self) -> None:
        host = self.host_var.get().strip()
        port = int(self.port_var.get().strip())
        cmd = self._build_pc_view_command()
        self._send_pc_view(host, port, cmd, update_status=True)

    def _build_pc_view_command(self) -> str:
        rx = self.rx_var.get().strip()
        ry = self.ry_var.get().strip()
        flipx = "1" if self.flipx_var.get() else "0"
        flipy = "1" if self.flipy_var.get() else "0"
        flipz = "1" if self.flipz_var.get() else "0"
        wire = "1" if self.wire_var.get() else "0"
        mesh = "1" if self.mesh_var.get() else "0"
        return f"pc_view rx={rx} ry={ry} flipx={flipx} flipy={flipy} flipz={flipz} wire={wire} mesh={mesh}"

    def _send_pc_view(self, host: str, port: int, cmd: str, update_status: bool = False) -> None:
        try:
            resp = send_command(host, port, cmd,
                                self.use_mtls_var.get(),
                                self.ca_file_var.get().strip(),
                                self.client_cert_var.get().strip(),
                                self.client_key_var.get().strip())
            if update_status:
                self.status.set(resp or "OK")
        except Exception as e:
            if update_status:
                messagebox.showerror("Connection Error", str(e))

    def _send_pc_view_async(self, host: str, port: int, cmd: str) -> None:
        threading.Thread(target=self._send_pc_view,
                         args=(host, port, cmd),
                         kwargs={"update_status": False},
                         daemon=True).start()

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
        hint = ttk.Label(win, text="Drag Left Mouse: Rotate View")
        hint.pack(anchor="w", padx=4, pady=(0, 4))

        def on_close():
            self._stream_stop.set()
            win.destroy()

        win.protocol("WM_DELETE_WINDOW", on_close)
        self._stream_stop.clear()

        def clamp(v: float, lo: float, hi: float) -> float:
            return max(lo, min(hi, v))

        def wrap_deg(v: float) -> float:
            while v > 180.0:
                v -= 360.0
            while v < -180.0:
                v += 360.0
            return v

        def on_mouse_down(event):
            self._dragging = True
            self._last_mouse_x = event.x
            self._last_mouse_y = event.y

        def on_mouse_up(_event):
            self._dragging = False

        def on_mouse_drag(event):
            if not self._dragging:
                return
            dx = event.x - self._last_mouse_x
            dy = event.y - self._last_mouse_y
            self._last_mouse_x = event.x
            self._last_mouse_y = event.y

            sensitivity = 0.22
            try:
                rx = float(self.rx_var.get().strip())
                ry = float(self.ry_var.get().strip())
            except Exception:
                rx = -20.0
                ry = 35.0

            rx = clamp(rx + dy * sensitivity, -89.0, 89.0)
            ry = wrap_deg(ry + dx * sensitivity)
            self.rx_var.set(f"{rx:.2f}")
            self.ry_var.set(f"{ry:.2f}")

            now = time.monotonic()
            if (now - self._last_drag_send_ts) >= 0.08:
                self._last_drag_send_ts = now
                host_now = self.host_var.get().strip()
                port_now = int(self.port_var.get().strip())
                cmd_now = self._build_pc_view_command()
                self._send_pc_view_async(host_now, port_now, cmd_now)

        canvas.bind("<ButtonPress-1>", on_mouse_down)
        canvas.bind("<ButtonRelease-1>", on_mouse_up)
        canvas.bind("<B1-Motion>", on_mouse_drag)

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
                with connect_socket(host, port, STREAM_CONNECT_TIMEOUT_SEC,
                                    self.use_mtls_var.get(),
                                    self.ca_file_var.get().strip(),
                                    self.client_cert_var.get().strip(),
                                    self.client_key_var.get().strip()) as s:
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

    def on_view_pc_client(self) -> None:
        if self._stream_thread and self._stream_thread.is_alive():
            messagebox.showinfo("PC Viewer", "A stream viewer is already running.")
            return

        host = self.host_var.get().strip()
        port = int(self.port_var.get().strip())

        win = tk.Toplevel(self)
        win.title("PointCloud View (Client Rendered)")
        width, height = 640, 480
        canvas = tk.Canvas(win, width=width, height=height, bg="black")
        canvas.pack()
        hint = ttk.Label(win, text="Drag: Rotate | Ctrl+Drag: Pan | Mouse Wheel: Zoom")
        hint.pack(anchor="w", padx=4, pady=(0, 4))

        local_rx = -20.0
        local_ry = 35.0
        local_zoom = 1.0
        local_pan_x = 0.0
        local_pan_y = 0.0
        dragging = False
        last_x = 0
        last_y = 0

        def on_close():
            self._stream_stop.set()
            win.destroy()

        win.protocol("WM_DELETE_WINDOW", on_close)
        self._stream_stop.clear()

        def clamp(v: float, lo: float, hi: float) -> float:
            return max(lo, min(hi, v))

        def wrap_deg(v: float) -> float:
            while v > 180.0:
                v -= 360.0
            while v < -180.0:
                v += 360.0
            return v

        def on_mouse_down(event):
            nonlocal dragging, last_x, last_y
            dragging = True
            last_x = event.x
            last_y = event.y

        def on_mouse_up(_event):
            nonlocal dragging
            dragging = False

        def on_mouse_drag(event):
            nonlocal last_x, last_y, local_rx, local_ry, local_pan_x, local_pan_y
            if not dragging:
                return
            dx = event.x - last_x
            dy = event.y - last_y
            last_x = event.x
            last_y = event.y
            ctrl_pressed = (event.state & 0x0004) != 0
            if ctrl_pressed:
                local_pan_x += dx
                local_pan_y += dy
            else:
                local_rx = clamp(local_rx + dy * 0.22, -89.0, 89.0)
                local_ry = wrap_deg(local_ry + dx * 0.22)

        def on_wheel(event):
            nonlocal local_zoom
            delta = 0
            if hasattr(event, "delta") and event.delta:
                delta = 1 if event.delta > 0 else -1
            elif getattr(event, "num", 0) == 4:
                delta = 1
            elif getattr(event, "num", 0) == 5:
                delta = -1
            if delta != 0:
                local_zoom = clamp(local_zoom * (1.0 + 0.08 * delta), 0.4, 3.0)

        canvas.bind("<ButtonPress-1>", on_mouse_down)
        canvas.bind("<ButtonRelease-1>", on_mouse_up)
        canvas.bind("<B1-Motion>", on_mouse_drag)
        canvas.bind("<MouseWheel>", on_wheel)
        canvas.bind("<Button-4>", on_wheel)
        canvas.bind("<Button-5>", on_wheel)

        def recv_exact(sock, n):
            buf = bytearray()
            while len(buf) < n:
                chunk = sock.recv(n - len(buf))
                if not chunk:
                    raise ConnectionError("Socket closed")
                buf.extend(chunk)
            return bytes(buf)

        def render_rgbd_ppm(depth_bytes: bytes, bgr_bytes: bytes, w: int, h: int) -> bytes:
            nonlocal local_rx, local_ry, local_zoom, local_pan_x, local_pan_y
            pixels = bytearray(width * height * 3)
            depth = memoryview(depth_bytes).cast("f")
            use_wire = self.wire_var.get()
            bgr = memoryview(bgr_bytes)

            hfov = math.radians(109.0)
            vfov = math.radians(55.0)
            fx = (w * 0.5) / math.tan(hfov * 0.5)
            fy = (h * 0.5) / math.tan(vfov * 0.5)
            cx = w * 0.5
            cy = h * 0.5

            rx = math.radians(local_rx)
            ry = math.radians(local_ry)
            cyr = math.cos(ry)
            syr = math.sin(ry)
            cxr = math.cos(rx)
            sxr = math.sin(rx)

            stride = 3
            min_depth = 0.1
            max_depth = 80.0
            points = []
            min_x = min_y = min_z = None
            max_x = max_y = max_z = None

            gx = (w + stride - 1) // stride
            gy = (h + stride - 1) // stride
            grid = [None] * (gx * gy)

            for y in range(0, h, stride):
                row = y * w
                for x in range(0, w, stride):
                    z = float(depth[row + x])
                    if z < min_depth or z > max_depth:
                        continue
                    X = (x - cx) * z / fx
                    Y = (y - cy) * z / fy
                    Z = z

                    x1 = X * cyr + Z * syr
                    z1 = -X * syr + Z * cyr
                    y1 = Y * cxr - z1 * sxr
                    z2 = Y * sxr + z1 * cxr
                    points.append((x, y, x1, y1, z2, z))

                    if min_x is None:
                        min_x = max_x = x1
                        min_y = max_y = y1
                        min_z = max_z = z2
                    else:
                        min_x = min(min_x, x1)
                        max_x = max(max_x, x1)
                        min_y = min(min_y, y1)
                        max_y = max(max_y, y1)
                        min_z = min(min_z, z2)
                        max_z = max(max_z, z2)

            if not points:
                header = f"P6 {width} {height} 255\n".encode("ascii")
                return header + bytes(pixels)

            pad = 10.0
            scale_x = (width - 2.0 * pad) / (max_x - min_x + 1e-6)
            scale_y = (height - 2.0 * pad) / (max_y - min_y + 1e-6)
            scale = min(scale_x, scale_y) * local_zoom

            def set_px(px: int, py: int, r: int, g: int, b: int) -> None:
                if px < 0 or px >= width or py < 0 or py >= height:
                    return
                p = (py * width + px) * 3
                pixels[p] = r
                pixels[p + 1] = g
                pixels[p + 2] = b

            def draw_line(x0: int, y0: int, x1: int, y1: int, r: int, g: int, b: int) -> None:
                dx = abs(x1 - x0)
                dy = abs(y1 - y0)
                sx = 1 if x0 < x1 else -1
                sy = 1 if y0 < y1 else -1
                err = dx - dy
                while True:
                    set_px(x0, y0, r, g, b)
                    if x0 == x1 and y0 == y1:
                        break
                    e2 = err * 2
                    if e2 > -dy:
                        err -= dy
                        x0 += sx
                    if e2 < dx:
                        err += dx
                        y0 += sy

            for (x, y, x1, y1, z2, z) in points:
                sx = int((x1 - min_x) * scale + pad + local_pan_x)
                sy = int(height - ((y1 - min_y) * scale + pad) + local_pan_y)
                if sx < 0 or sx >= width or sy < 0 or sy >= height:
                    continue

                idx3 = (y * w + x) * 3
                if idx3 + 2 < len(bgr):
                    b = int(bgr[idx3 + 0])
                    g = int(bgr[idx3 + 1])
                    r = int(bgr[idx3 + 2])
                else:
                    r = g = b = 0

                set_px(sx, sy, r, g, b)
                gx_i = x // stride
                gy_i = y // stride
                if 0 <= gx_i < gx and 0 <= gy_i < gy:
                    grid[gy_i * gx + gx_i] = (sx, sy, z, r, g, b)

            if use_wire:
                depth_jump_ratio = 0.18

                def close_depth(a: float, b: float) -> bool:
                    m = max(a, b)
                    if m <= 1e-6:
                        return False
                    return abs(a - b) / m <= depth_jump_ratio

                for gy_i in range(gy):
                    row_off = gy_i * gx
                    for gx_i in range(gx):
                        p0 = grid[row_off + gx_i]
                        if p0 is None:
                            continue
                        x0, y0, z0, r0, g0, b0 = p0
                        if gx_i + 1 < gx:
                            p1 = grid[row_off + gx_i + 1]
                            if p1 is not None and close_depth(z0, p1[2]):
                                r1, g1, b1 = p1[3], p1[4], p1[5]
                                draw_line(x0, y0, p1[0], p1[1],
                                          (r0 + r1) // 2, (g0 + g1) // 2, (b0 + b1) // 2)
                        if gy_i + 1 < gy:
                            p2 = grid[(gy_i + 1) * gx + gx_i]
                            if p2 is not None and close_depth(z0, p2[2]):
                                r2, g2, b2 = p2[3], p2[4], p2[5]
                                draw_line(x0, y0, p2[0], p2[1],
                                          (r0 + r2) // 2, (g0 + g2) // 2, (b0 + b2) // 2)

            header = f"P6 {width} {height} 255\n".encode("ascii")
            return header + bytes(pixels)

        ppm_error_reported = False

        def draw_ppm(ppm_bytes: bytes):
            nonlocal ppm_error_reported
            if not win.winfo_exists():
                return
            try:
                # For PPM, Tk expects raw PPM bytes (not base64-encoded payload).
                img = tk.PhotoImage(data=ppm_bytes, format="PPM")
                canvas.delete("all")
                canvas.create_image(0, 0, image=img, anchor="nw")
                canvas.image = img
                ppm_error_reported = False
            except Exception as e:
                if not ppm_error_reported:
                    ppm_error_reported = True
                    self.status.set(f"Client render error: {e}")

        def stream_loop():
            try:
                with connect_socket(host, port, STREAM_CONNECT_TIMEOUT_SEC,
                                    self.use_mtls_var.get(),
                                    self.ca_file_var.get().strip(),
                                    self.client_cert_var.get().strip(),
                                    self.client_key_var.get().strip()) as s:
                    s.sendall(b"rgbd_stream\n")
                    line = b""
                    while not line.endswith(b"\n"):
                        line += s.recv(1)
                    if not line.startswith(b"OK rgbd_stream"):
                        raise RuntimeError(f"Unexpected response: {line.decode(errors='ignore')}")
                    s.settimeout(None)

                    while not self._stream_stop.is_set():
                        header = recv_exact(s, 20)
                        _frame_idx, w, h, depth_bytes, bgr_bytes = struct.unpack("<IIIII", header)
                        depth_data = recv_exact(s, depth_bytes)
                        bgr_data = recv_exact(s, bgr_bytes)
                        ppm = render_rgbd_ppm(depth_data, bgr_data, w, h)
                        self.after(0, draw_ppm, ppm)
            except Exception as e:
                if not self._stream_stop.is_set():
                    self.after(0, lambda: messagebox.showerror("RGBD Stream Error", str(e)))

        self._stream_thread = threading.Thread(target=stream_loop, daemon=True)
        self._stream_thread.start()


if __name__ == "__main__":
    app = ClientGui()
    app.mainloop()
