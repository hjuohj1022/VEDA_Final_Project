import argparse
import os
import ssl
import struct
import time
import traceback

import cv2
import numpy as np
import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion

try:
    from app_client_config import MQTT_BROKER, MQTT_PORT, MQTT_TLS_INSECURE
except ImportError:
    MQTT_BROKER = ""
    MQTT_PORT = 8883
    MQTT_TLS_INSECURE = True

WIDTH = 160
HEIGHT = 120
FRAME_BYTES = WIDTH * HEIGHT * 2
HEADER_SIZE = 10
DEFAULT_TOPIC = "lepton/frame/chunk"
FRAME_TIMEOUT_SEC = 1.5


def log(message):
    print(message, flush=True)


class FrameAssembler:
    def __init__(self):
        self.reset()
        self.last_display_ts = None
        self.display_fps = 0.0
        self.total_messages = 0
        self.total_status_messages = 0
        self.total_chunk_messages = 0
        self.total_frames = 0
        self.last_chunk_log = time.monotonic()
        self.last_message_ts = time.monotonic()
        self.last_topic = "<none>"
        self.last_frame_view = None

    def reset(self):
        self.current_frame_id = None
        self.total_chunks = 0
        self.started_at = 0.0
        self.min_val = 0
        self.max_val = 0
        self.chunks = {}

    def push(self, payload):
        self.total_messages += 1
        self.total_chunk_messages += 1
        self.last_message_ts = time.monotonic()

        if len(payload) < HEADER_SIZE:
            return None

        now = time.monotonic()
        if self.current_frame_id is not None and (now - self.started_at) > FRAME_TIMEOUT_SEC:
            log(f"frame timeout: id={self.current_frame_id}")
            self.reset()

        frame_id, idx, total, min_val, max_val = struct.unpack(">HHHHH", payload[:HEADER_SIZE])
        chunk = payload[HEADER_SIZE:]

        if total <= 0 or total > 100:
            return None

        if self.current_frame_id is None or frame_id != self.current_frame_id:
            if self.current_frame_id is not None and len(self.chunks) != self.total_chunks:
                log(f"drop incomplete frame: id={self.current_frame_id}")
            self.current_frame_id = frame_id
            self.total_chunks = total
            self.started_at = now
            self.min_val = min_val
            self.max_val = max_val
            self.chunks = {}

        self.chunks[idx] = chunk

        if (time.monotonic() - self.last_chunk_log) >= 1.0:
            log(
                f"chunks total={self.total_messages} current_frame={self.current_frame_id} "
                f"assembled={len(self.chunks)}/{self.total_chunks}"
            )
            self.last_chunk_log = time.monotonic()

        if len(self.chunks) != self.total_chunks:
            return None

        full_data = bytearray()
        for chunk_idx in range(self.total_chunks):
            chunk_data = self.chunks.get(chunk_idx)
            if chunk_data is None:
                self.reset()
                return None
            full_data.extend(chunk_data)

        frame_id = self.current_frame_id
        min_val = self.min_val
        max_val = self.max_val
        self.total_frames += 1
        self.reset()

        if len(full_data) < FRAME_BYTES:
            log(f"incomplete frame bytes: id={frame_id} bytes={len(full_data)}")
            return None

        return frame_id, min_val, max_val, bytes(full_data[:FRAME_BYTES])

    def update_display_fps(self):
        now = time.perf_counter()
        if self.last_display_ts is not None:
            delta = now - self.last_display_ts
            if delta > 0.0:
                instant = 1.0 / delta
                self.display_fps = instant if self.display_fps == 0.0 else ((self.display_fps * 0.8) + (instant * 0.2))
        self.last_display_ts = now


def normalize_frame(raw_frame, min_val, max_val):
    valid_mask = (raw_frame > 1000) & (raw_frame < 30000)
    valid_pixels = raw_frame[valid_mask]

    if valid_pixels.size > 0:
        lo = float(np.percentile(valid_pixels, 2))
        hi = float(np.percentile(valid_pixels, 98))
        if (hi - lo) < 100.0:
            hi = lo + 100.0
    else:
        lo = float(min_val)
        hi = float(max_val if max_val > min_val else min_val + 100)

    scaled = ((raw_frame.astype(np.float32) - lo) / (hi - lo)) * 255.0
    scaled = np.clip(scaled, 0, 255).astype(np.uint8)
    return scaled, int(lo), int(hi)


def render_frame(assembler, frame_id, min_val, max_val, frame_bytes):
    raw_frame = np.frombuffer(frame_bytes, dtype=">u2").reshape((HEIGHT, WIDTH))
    gray_frame, lo, hi = normalize_frame(raw_frame, min_val, max_val)
    color_frame = cv2.applyColorMap(gray_frame, cv2.COLORMAP_JET)
    view = cv2.resize(color_frame, (960, 720), interpolation=cv2.INTER_LINEAR)

    assembler.update_display_fps()
    cv2.putText(view, f"frame {frame_id}", (20, 36), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (255, 255, 255), 2)
    cv2.putText(view, f"range {lo}-{hi}", (20, 70), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (255, 255, 255), 2)
    cv2.putText(view, f"display fps {assembler.display_fps:.2f}", (20, 104), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (255, 255, 255), 2)

    assembler.last_frame_view = view.copy()
    cv2.imshow("Thermal MQTT Test", view)
    cv2.waitKey(1)


def render_wait_screen(assembler, topic_text):
    if assembler.last_frame_view is not None:
        canvas = assembler.last_frame_view.copy()
    else:
        canvas = np.zeros((720, 960, 3), dtype=np.uint8)
    age = time.monotonic() - assembler.last_message_ts
    cv2.rectangle(canvas, (0, 0), (960, 150), (0, 0, 0), -1)
    cv2.putText(canvas, "Thermal MQTT Test", (20, 36), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (255, 255, 255), 2)
    cv2.putText(canvas, f"messages={assembler.total_messages} status={assembler.total_status_messages} chunks={assembler.total_chunk_messages} frames={assembler.total_frames}", (20, 70), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (200, 200, 200), 2)
    cv2.putText(canvas, f"last topic={topic_text} idle={age:.1f}s", (20, 102), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (180, 180, 180), 2)
    cv2.putText(canvas, "Waiting for next complete frame...", (20, 134), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 200, 255), 2)
    cv2.imshow("Thermal MQTT Test", canvas)
    cv2.waitKey(1)


def build_client(args, assembler):
    cert_dir = os.path.join(os.path.dirname(__file__), "..", "certs")
    ca_cert = os.path.join(cert_dir, "rootCA.crt")
    client_cert = os.path.join(cert_dir, "client-stm32.crt")
    client_key = os.path.join(cert_dir, "client-stm32.key")

    client = mqtt.Client(callback_api_version=CallbackAPIVersion.VERSION2)

    def on_connect(cli, userdata, flags, rc, properties=None):
        del userdata, flags, properties
        if rc == 0:
            log(f"connected to {args.host}:{args.port}")
            cli.subscribe("lepton/#")
            log("subscribed: lepton/#")
        else:
            log(f"connect failed: rc={rc}")

    def on_disconnect(cli, userdata, flags, rc, properties=None):
        del cli, userdata, flags, properties
        log(f"disconnected: rc={rc}")

    def on_message(cli, userdata, msg):
        del cli, userdata
        assembler.last_message_ts = time.monotonic()
        assembler.last_topic = msg.topic
        if msg.topic == "lepton/status":
            assembler.total_messages += 1
            assembler.total_status_messages += 1
            try:
                status_text = msg.payload.decode("utf-8", errors="replace")
            except Exception:
                status_text = "<decode failed>"
            log(f"status message: {status_text}")
            if assembler.last_frame_view is None:
                render_wait_screen(assembler, msg.topic)
            return

        if msg.topic != args.topic:
            assembler.total_messages += 1
            log(f"other topic: {msg.topic} payload_len={len(msg.payload)}")
            if assembler.last_frame_view is None:
                render_wait_screen(assembler, msg.topic)
            return

        result = assembler.push(msg.payload)
        if result is None:
            if assembler.last_frame_view is None or (time.monotonic() - assembler.last_display_ts if assembler.last_display_ts else 999.0) > 1.0:
                render_wait_screen(assembler, msg.topic)
            return
        frame_id, min_val, max_val, frame_bytes = result
        log(f"frame complete: id={frame_id} total_frames={assembler.total_frames}")
        render_frame(assembler, frame_id, min_val, max_val, frame_bytes)

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message

    if args.tls:
        client.tls_set(
            ca_certs=ca_cert,
            certfile=client_cert,
            keyfile=client_key,
            cert_reqs=ssl.CERT_REQUIRED,
            tls_version=ssl.PROTOCOL_TLSv1_2,
        )
        client.tls_insecure_set(args.insecure)

    return client


def parse_args():
    parser = argparse.ArgumentParser(description="Simple thermal MQTT viewer")
    parser.add_argument("--host", default=MQTT_BROKER)
    parser.add_argument("--port", type=int, default=MQTT_PORT)
    parser.add_argument("--topic", default=DEFAULT_TOPIC)
    parser.add_argument("--tls", action="store_true", default=True)
    parser.add_argument("--no-tls", dest="tls", action="store_false")
    parser.add_argument("--insecure", action="store_true", default=MQTT_TLS_INSECURE)
    parser.add_argument("--secure-cn", dest="insecure", action="store_false")
    return parser.parse_args()


def main():
    try:
        args = parse_args()
        if not args.host:
            raise RuntimeError("Create app_client_config.py from app_client_config.example.py")
        log("thermal_view_test.py start")
        log(f"python cwd={os.getcwd()}")
        log(f"connecting to {args.host}:{args.port} topic={args.topic} tls={args.tls} insecure={args.insecure}")

        assembler = FrameAssembler()
        render_wait_screen(assembler, "<none>")
        client = build_client(args, assembler)
        client.connect(args.host, args.port, 60)
        client.loop_forever()
    except KeyboardInterrupt:
        log("stopped")
    except Exception as exc:
        log(f"fatal error: {exc}")
        traceback.print_exc()
        try:
            input("press enter to exit...")
        except EOFError:
            pass
    finally:
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
