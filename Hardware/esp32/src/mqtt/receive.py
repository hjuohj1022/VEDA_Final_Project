import os
import ssl
import struct
import time

import cv2
import numpy as np
import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion

MQTT_BROKER = "192.168.55.200"
MQTT_PORT = 8883
MQTT_TOPIC = "lepton/frame/chunk"

CERT_DIR = os.path.join(os.path.dirname(__file__), "..", "certs")
CA_CERT = os.path.join(CERT_DIR, "rootCA.crt")
CLIENT_CERT = os.path.join(CERT_DIR, "client-stm32.crt")
CLIENT_KEY = os.path.join(CERT_DIR, "client-stm32.key")

WIDTH = 160
HEIGHT = 120
FRAME_BYTES = WIDTH * HEIGHT * 2
HEADER_SIZE = 10
FRAME_TIMEOUT_SEC = 1.5

current_frame_chunks = {}
current_frame_id = None
total_chunks_expected = 0
current_frame_started_at = 0.0


def reset_frame_state():
    global current_frame_chunks
    global current_frame_id
    global total_chunks_expected
    global current_frame_started_at

    current_frame_chunks = {}
    current_frame_id = None
    total_chunks_expected = 0
    current_frame_started_at = 0.0


def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print("MQTTS connected")
        client.subscribe(MQTT_TOPIC)
        print(f"Subscribed to {MQTT_TOPIC}")
    else:
        print(f"Connection failed with code {rc}")


def on_disconnect(client, userdata, flags, rc, properties=None):
    print(f"Disconnected with result code {rc}, reconnecting...")


def on_message(client, userdata, msg):
    global current_frame_chunks
    global current_frame_id
    global total_chunks_expected
    global current_frame_started_at

    if len(msg.payload) < HEADER_SIZE:
        print(f"Small payload: {len(msg.payload)}")
        return

    now = time.monotonic()
    if (current_frame_id is not None) and ((now - current_frame_started_at) > FRAME_TIMEOUT_SEC):
        print(f"Frame timeout: id={current_frame_id}, dropping incomplete frame")
        reset_frame_state()

    header = msg.payload[:HEADER_SIZE]
    frame_id, idx, total, min_val, max_val = struct.unpack(">HHHHH", header)
    payload = msg.payload[HEADER_SIZE:]

    if (total <= 0) or (total > 100):
        return

    if (current_frame_id is None) or (frame_id != current_frame_id):
        if current_frame_id is not None and len(current_frame_chunks) != total_chunks_expected:
            print(f"Switching frames: drop incomplete frame id={current_frame_id}")
        current_frame_chunks = {}
        current_frame_id = frame_id
        total_chunks_expected = total
        current_frame_started_at = now

    current_frame_chunks[idx] = payload

    if idx % 5 == 0 or idx == total - 1:
        print(
            f"[frame {frame_id}] chunk {idx + 1}/{total} "
            f"min={min_val} max={max_val} data={len(payload)}"
        )

    if len(current_frame_chunks) == total_chunks_expected:
        frame_data_copy = dict(current_frame_chunks)
        completed_frame_id = current_frame_id
        reset_frame_state()
        print(f"Frame complete: id={completed_frame_id}")
        assemble_and_display(frame_data_copy, total, min_val, max_val, completed_frame_id)


def assemble_and_display(chunks, total, min_val, max_val, frame_id):
    full_data = bytearray()
    for i in range(total):
        if i not in chunks:
            print(f"Missing chunk {i}, dropping frame id={frame_id}")
            return
        full_data.extend(chunks[i])

    if len(full_data) < FRAME_BYTES:
        print(f"Incomplete data for frame {frame_id}: {len(full_data)} / {FRAME_BYTES}")
        return

    raw_frame = np.frombuffer(full_data[:FRAME_BYTES], dtype=">u2").reshape((HEIGHT, WIDTH))
    print(
        f"DEBUG frame={frame_id} raw stats: "
        f"min={raw_frame.min()} max={raw_frame.max()} avg={raw_frame.mean():.1f}"
    )

    valid_mask = (raw_frame > 1000) & (raw_frame < 30000)
    valid_pixels = raw_frame[valid_mask]

    if len(valid_pixels) > 0:
        f_min = np.percentile(valid_pixels, 2)
        f_max = np.percentile(valid_pixels, 98)
        if f_max - f_min < 100:
            f_max = f_min + 100
    else:
        f_min, f_max = min_val, max_val

    if f_max > f_min:
        display_frame = (raw_frame.astype(np.float32) - f_min) / (f_max - f_min) * 255
        display_frame = np.clip(display_frame, 0, 255).astype(np.uint8)
    else:
        display_frame = np.zeros((HEIGHT, WIDTH), dtype=np.uint8)

    color_frame = cv2.applyColorMap(display_frame, cv2.COLORMAP_JET)
    resized_frame = cv2.resize(color_frame, (800, 600), interpolation=cv2.INTER_LINEAR)

    info_text = f"Frame:{frame_id} Min:{int(f_min)} Max:{int(f_max)}"
    cv2.putText(
        resized_frame,
        info_text,
        (20, 40),
        cv2.FONT_HERSHEY_DUPLEX,
        0.8,
        (255, 255, 255),
        1,
    )

    cv2.imshow("Thermal Stream (Secure)", resized_frame)
    cv2.waitKey(1)


client = mqtt.Client(callback_api_version=CallbackAPIVersion.VERSION2)
client.on_connect = on_connect
client.on_message = on_message
client.on_disconnect = on_disconnect

try:
    client.tls_set(
        ca_certs=CA_CERT,
        certfile=CLIENT_CERT,
        keyfile=CLIENT_KEY,
        cert_reqs=ssl.CERT_REQUIRED,
        tls_version=ssl.PROTOCOL_TLSv1_2,
    )
    client.tls_insecure_set(True)
except Exception as exc:
    print(f"TLS setup error: {exc}")
    raise SystemExit(1)

print(f"Connecting to MQTTS broker at {MQTT_BROKER}:{MQTT_PORT}...")

connected = False
while not connected:
    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        connected = True
    except Exception as exc:
        print(f"Initial connection failed: {exc}. Retrying in 5 seconds...")
        time.sleep(5)

try:
    client.loop_forever()
except KeyboardInterrupt:
    print("\nStopped by user")
finally:
    cv2.destroyAllWindows()
