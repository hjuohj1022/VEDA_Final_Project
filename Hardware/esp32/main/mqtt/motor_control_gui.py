import tkinter as tk
from tkinter import messagebox
import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion
import ssl
import os

try:
    from app_client_config import MQTT_BROKER, MQTT_PORT, MQTT_TLS_INSECURE
except ImportError:
    MQTT_BROKER = ""
    MQTT_PORT = 8883
    MQTT_TLS_INSECURE = True

# --- MQTT Configuration ---
MQTT_TOPIC = "motor/control"      # Topic the ESP32 is subscribed to
RESP_TOPIC = "motor/response"

# Certificate Paths (Relative to this file)
CERT_DIR = os.path.join(os.path.dirname(__file__), "..", "certs")
CA_CERT     = os.path.join(CERT_DIR, "rootCA.crt")
CLIENT_CERT = os.path.join(CERT_DIR, "client-stm32.crt")
CLIENT_KEY  = os.path.join(CERT_DIR, "client-stm32.key")

class MotorControlApp:
    def __init__(self, root):
        self.root = root
        self.root.title("STM32 Motor Control Client")
        self.root.geometry("400x300")

        # MQTT Client Setup
        self.client = mqtt.Client(callback_api_version=CallbackAPIVersion.VERSION2)
        self.client.on_connect = self.on_connect
        self.client.on_disconnect = self.on_disconnect
        self.client.on_message = self.on_message
        self.setup_mqtt()

        # GUI Layout
        self.create_widgets()

        # Connect to Broker
        try:
            if not MQTT_BROKER:
                raise RuntimeError("Create app_client_config.py from app_client_config.example.py")
            self.client.connect(MQTT_BROKER, MQTT_PORT, 60)
            self.client.loop_start()
        except Exception as e:
            messagebox.showerror("Connection Error", f"Failed to connect to MQTT Broker: {e}")

    def setup_mqtt(self):
        try:
            self.client.tls_set(
                ca_certs=CA_CERT,
                certfile=CLIENT_CERT,
                keyfile=CLIENT_KEY,
                cert_reqs=ssl.CERT_REQUIRED,
                tls_version=ssl.PROTOCOL_TLSv1_2
            )
            # Depending on your broker setup, you might need this:
            self.client.tls_insecure_set(MQTT_TLS_INSECURE)
        except Exception as e:
            print(f"TLS Setup Error: {e}")

    def send_command(self, cmd):
        print(f"Sending: {cmd}")
        info = self.client.publish(MQTT_TOPIC, cmd, qos=1)
        self.status_var.set(f"TX: {cmd} rc={info.rc}")

    def on_connect(self, client, userdata, flags, reason_code, properties=None):
        del client, userdata, flags, properties
        print(f"MQTT connected rc={reason_code}")
        if reason_code == 0:
            self.client.subscribe(RESP_TOPIC, qos=0)
            self.status_var.set("MQTT connected")
        else:
            self.status_var.set(f"MQTT connect failed rc={reason_code}")

    def on_disconnect(self, client, userdata, flags, reason_code, properties=None):
        del client, userdata, flags, properties
        print(f"MQTT disconnected rc={reason_code}")
        self.status_var.set(f"Disconnected rc={reason_code}")

    def on_message(self, client, userdata, msg):
        del client, userdata
        payload = msg.payload.decode("utf-8", errors="replace").strip()
        print(f"RX {msg.topic}: {payload}")
        self.status_var.set(f"{msg.topic}: {payload}")

    def on_press(self, motor_id, direction):
        # direction: 'left' or 'right'
        cmd = f"motor{motor_id} {direction} press"
        self.send_command(cmd)

    def on_release(self, motor_id):
        cmd = f"motor{motor_id} release"
        self.send_command(cmd)

    def on_step(self, motor_id, direction, delta=10):
        sign = "left" if direction == "left" else "right"
        cmd = f"motor{motor_id} {sign} {delta}"
        self.send_command(cmd)

    def on_set(self, motor_id, angle):
        cmd = f"motor{motor_id} set {angle}"
        self.send_command(cmd)

    def on_read(self):
        self.send_command("read")

    def on_stop_all(self):
        self.send_command("stopall")

    def create_widgets(self):
        main_frame = tk.Frame(self.root, padx=20, pady=20)
        main_frame.pack(expand=True, fill="both")

        for i in range(1, 4):  # Motor 1, 2, 3
            row_frame = tk.Frame(main_frame, pady=10)
            row_frame.pack(fill="x")

            lbl = tk.Label(row_frame, text=f"Motor {i}", width=10, font=("Arial", 10, "bold"))
            lbl.pack(side="left")

            btn_step_left = tk.Button(row_frame, text="-10", width=6, bg="#d7ecff",
                                      command=lambda m=i: self.on_step(m, "left", 10))
            btn_step_left.pack(side="left", padx=2)

            # Left Button
            btn_left = tk.Button(row_frame, text="Hold L", width=8, bg="lightblue")
            btn_left.pack(side="left", padx=5)
            btn_left.bind("<ButtonPress-1>", lambda event, m=i: self.on_press(m, "left"))
            btn_left.bind("<ButtonRelease-1>", lambda event, m=i: self.on_release(m))

            btn_stop = tk.Button(row_frame, text="Stop", width=6, bg="#ffd9d9",
                                 command=lambda m=i: self.on_release(m))
            btn_stop.pack(side="left", padx=2)

            # Right Button
            btn_right = tk.Button(row_frame, text="Hold R", width=8, bg="lightgreen")
            btn_right.pack(side="left", padx=5)
            btn_right.bind("<ButtonPress-1>", lambda event, m=i: self.on_press(m, "right"))
            btn_right.bind("<ButtonRelease-1>", lambda event, m=i: self.on_release(m))

            btn_step_right = tk.Button(row_frame, text="+10", width=6, bg="#dcffd7",
                                       command=lambda m=i: self.on_step(m, "right", 10))
            btn_step_right.pack(side="left", padx=2)

        utility_frame = tk.Frame(main_frame, pady=12)
        utility_frame.pack(fill="x")

        btn_center = tk.Button(utility_frame, text="Center All", width=12,
                               command=lambda: [self.on_set(1, 90), self.on_set(2, 90), self.on_set(3, 90)])
        btn_center.pack(side="left", padx=4)

        btn_read = tk.Button(utility_frame, text="Read Angles", width=12, command=self.on_read)
        btn_read.pack(side="left", padx=4)

        btn_stop_all = tk.Button(utility_frame, text="Stop All", width=12, command=self.on_stop_all)
        btn_stop_all.pack(side="left", padx=4)

        # Status Bar
        self.status_var = tk.StringVar(value="Connecting...")
        status_bar = tk.Label(self.root, textvariable=self.status_var, bd=1, relief=tk.SUNKEN, anchor=tk.W)
        status_bar.pack(side=tk.BOTTOM, fill=tk.X)
        hint_bar = tk.Label(self.root, text="Hold L/R for continuous move, use -10/+10 for one-shot test", anchor=tk.W)
        hint_bar.pack(side=tk.BOTTOM, fill=tk.X)

    def __del__(self):
        self.client.loop_stop()
        self.client.disconnect()

if __name__ == "__main__":
    root = tk.Tk()
    app = MotorControlApp(root)
    root.mainloop()
