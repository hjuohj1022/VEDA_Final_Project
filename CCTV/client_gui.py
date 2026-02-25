import socket
import tkinter as tk
from tkinter import ttk, messagebox


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
        self.title("Entrance Client")
        self.geometry("360x240")
        self.resizable(False, False)

        self.host_var = tk.StringVar(value="127.0.0.1")
        self.port_var = tk.StringVar(value="9090")
        self.channel_var = tk.StringVar(value="0")
        self.mode_var = tk.StringVar(value="headless")

        frm = ttk.Frame(self, padding=12)
        frm.pack(fill=tk.BOTH, expand=True)

        ttk.Label(frm, text="Host").grid(row=0, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.host_var, width=18).grid(row=0, column=1, sticky="w")

        ttk.Label(frm, text="Port").grid(row=1, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.port_var, width=10).grid(row=1, column=1, sticky="w")

        ttk.Label(frm, text="Channel").grid(row=2, column=0, sticky="w")
        channel_box = ttk.Combobox(frm, textvariable=self.channel_var, width=8, state="readonly")
        channel_box["values"] = ("0", "1", "2", "3")
        channel_box.grid(row=2, column=1, sticky="w")

        ttk.Label(frm, text="Mode").grid(row=3, column=0, sticky="w")
        ttk.Radiobutton(frm, text="Headless", variable=self.mode_var, value="headless").grid(row=3, column=1, sticky="w")
        ttk.Radiobutton(frm, text="GUI", variable=self.mode_var, value="gui").grid(row=4, column=1, sticky="w")

        btn_frame = ttk.Frame(frm)
        btn_frame.grid(row=5, column=0, columnspan=2, pady=12, sticky="w")

        ttk.Button(btn_frame, text="Start", command=self.on_start).grid(row=0, column=0, padx=(0, 8))
        ttk.Button(btn_frame, text="Stop", command=self.on_stop).grid(row=0, column=1)

        self.status = tk.StringVar(value="Idle")
        ttk.Label(frm, textvariable=self.status, foreground="#1f5e9a").grid(row=6, column=0, columnspan=2, sticky="w")

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
            messagebox.showerror("Error", str(e))

    def on_stop(self) -> None:
        host = self.host_var.get().strip()
        port = int(self.port_var.get().strip())
        try:
            resp = send_command(host, port, "stop")
            self.status.set(resp or "OK")
        except Exception as e:
            messagebox.showerror("Error", str(e))


if __name__ == "__main__":
    app = ClientGui()
    app.mainloop()
