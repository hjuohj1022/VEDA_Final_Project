import tkinter as tk
from tkinter import messagebox
import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion
import ssl
import os

# --- MQTT Configuration ---
MQTT_BROKER = "192.168.55.200"  # Broker IP
MQTT_PORT = 8883               # MQTTS Secure Port
MQTT_TOPIC = "test/topic"      # Topic the ESP32 is subscribed to

# Certificate Paths (Relative to this file)
CERT_DIR = os.path.join(os.path.dirname(__file__), "..", "certs")
CA_CERT     = os.path.join(CERT_DIR, "ca_cert.pem")
CLIENT_CERT = os.path.join(CERT_DIR, "client_cert.pem")
CLIENT_KEY  = os.path.join(CERT_DIR, "client_key.pem")

class MotorControlApp:
    def __init__(self, root):
        self.root = root
        self.root.title("STM32 Motor Control Client")
        self.root.geometry("400x300")

        # MQTT Client Setup
        self.client = mqtt.Client(callback_api_version=CallbackAPIVersion.VERSION2)
        self.setup_mqtt()

        # GUI Layout
        self.create_widgets()

        # Connect to Broker
        try:
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
            self.client.tls_insecure_set(True) 
        except Exception as e:
            print(f"TLS Setup Error: {e}")

    def send_command(self, cmd):
        print(f"Sending: {cmd}")
        # QoS 1 ensures the broker receives the message at least once
        self.client.publish(MQTT_TOPIC, cmd, qos=1)

    def on_press(self, motor_id, direction):
        # direction: 'left' or 'right'
        cmd = f"motor{motor_id} {direction} press"
        self.send_command(cmd)

    def on_release(self, motor_id):
        cmd = f"motor{motor_id} release"
        self.send_command(cmd)

    def create_widgets(self):
        main_frame = tk.Frame(self.root, padx=20, pady=20)
        main_frame.pack(expand=True, fill="both")

        for i in range(1, 4):  # Motor 1, 2, 3
            row_frame = tk.Frame(main_frame, pady=10)
            row_frame.pack(fill="x")

            lbl = tk.Label(row_frame, text=f"Motor {i}", width=10, font=("Arial", 10, "bold"))
            lbl.pack(side="left")

            # Left Button
            btn_left = tk.Button(row_frame, text=f"Left", width=10, bg="lightblue")
            btn_left.pack(side="left", padx=5)
            # Bind press and release events
            btn_left.bind("<ButtonPress-1>", lambda event, m=i: self.on_press(m, "left"))
            btn_left.bind("<ButtonRelease-1>", lambda event, m=i: self.on_release(m))

            # Right Button
            btn_right = tk.Button(row_frame, text=f"Right", width=10, bg="lightgreen")
            btn_right.pack(side="left", padx=5)
            # Bind press and release events
            btn_right.bind("<ButtonPress-1>", lambda event, m=i: self.on_press(m, "right"))
            btn_right.bind("<ButtonRelease-1>", lambda event, m=i: self.on_release(m))

        # Status Bar
        self.status_var = tk.StringVar(value="Ready")
        status_bar = tk.Label(self.root, textvariable=self.status_var, bd=1, relief=tk.SUNKEN, anchor=tk.W)
        status_bar.pack(side=tk.BOTTOM, fill=tk.X)

    def __del__(self):
        self.client.loop_stop()
        self.client.disconnect()

if __name__ == "__main__":
    root = tk.Tk()
    app = MotorControlApp(root)
    root.mainloop()
