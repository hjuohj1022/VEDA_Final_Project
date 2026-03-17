import argparse
import socket
import struct
import time

import cv2
import numpy as np

WIDTH = 160
HEIGHT = 120
FRAME_BYTES = WIDTH * HEIGHT * 2
FRAME_BYTES_8 = WIDTH * HEIGHT
HEADER_SIZE = 10
DEFAULT_BIND = "192.168.55.14"
DEFAULT_PORT = 5005
FRAME_TIMEOUT_SEC = 1.0


def log(message):
    print(message, flush=True)


class FrameAssembler:
    def __init__(self):
        self.reset()
        self.total_packets = 0
        self.total_frames = 0
        self.display_fps = 0.0
        self.last_display_ts = None

    def reset(self):
        self.current_frame_id = None
        self.total_chunks = 0
        self.min_val = 0
        self.max_val = 0
        self.started_at = 0.0
        self.chunks = {}

    def push(self, payload):
        self.total_packets += 1

        if len(payload) < HEADER_SIZE:
            return None

        now = time.monotonic()
        if (self.current_frame_id is not None) and ((now - self.started_at) > FRAME_TIMEOUT_SEC):
            log(f"frame timeout: id={self.current_frame_id}")
            self.reset()

        frame_id, idx, total, min_val, max_val = struct.unpack(">HHHHH", payload[:HEADER_SIZE])
        chunk = payload[HEADER_SIZE:]

        if (total <= 0) or (total > 100):
            return None

        if (self.current_frame_id is None) or (frame_id != self.current_frame_id):
            self.current_frame_id = frame_id
            self.total_chunks = total
            self.min_val = min_val
            self.max_val = max_val
            self.started_at = now
            self.chunks = {}

        self.chunks[idx] = chunk

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

        if len(full_data) < FRAME_BYTES_8:
            log(f"incomplete frame bytes: id={frame_id} bytes={len(full_data)}")
            return None

        if len(full_data) >= FRAME_BYTES:
            return frame_id, min_val, max_val, bytes(full_data[:FRAME_BYTES]), 2

        return frame_id, min_val, max_val, bytes(full_data[:FRAME_BYTES_8]), 1

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


def normalize_frame_8bit(gray_frame):
    valid_pixels = gray_frame[gray_frame > 0]

    if valid_pixels.size > 0:
        lo = float(np.percentile(valid_pixels, 2))
        hi = float(np.percentile(valid_pixels, 98))
        if hi <= lo:
            hi = lo + 1.0
    else:
        lo = 0.0
        hi = 255.0

    scaled = ((gray_frame.astype(np.float32) - lo) / (hi - lo)) * 255.0
    scaled = np.clip(scaled, 0, 255).astype(np.uint8)
    return scaled, int(lo), int(hi)


def render_frame(assembler, frame_id, min_val, max_val, frame_bytes, bytes_per_pixel):
    if bytes_per_pixel == 1:
        raw_gray = np.frombuffer(frame_bytes[:FRAME_BYTES_8], dtype=np.uint8).reshape((HEIGHT, WIDTH))
        gray_frame, lo8, hi8 = normalize_frame_8bit(raw_gray)
        lo = int(min_val)
        hi = int(max_val)
    else:
        raw_frame = np.frombuffer(frame_bytes[:FRAME_BYTES], dtype=">u2").reshape((HEIGHT, WIDTH))
        gray_frame, lo, hi = normalize_frame(raw_frame, min_val, max_val)
    color_frame = cv2.applyColorMap(gray_frame, cv2.COLORMAP_JET)
    view = cv2.resize(color_frame, (960, 720), interpolation=cv2.INTER_LINEAR)

    assembler.update_display_fps()
    cv2.putText(view, f"frame {frame_id}", (20, 36), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (255, 255, 255), 2)
    cv2.putText(view, f"range {lo}-{hi}", (20, 70), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (255, 255, 255), 2)
    cv2.putText(view, f"udp fps {assembler.display_fps:.2f}", (20, 104), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (255, 255, 255), 2)
    cv2.putText(view, f"packets {assembler.total_packets} frames {assembler.total_frames}", (20, 138), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2)
    cv2.putText(view, f"bpp {bytes_per_pixel}", (20, 172), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2)
    if bytes_per_pixel == 1:
        cv2.putText(view, f"8bit stretch {lo8}-{hi8}", (20, 206), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2)
    cv2.imshow("Thermal UDP Test", view)
    cv2.waitKey(1)


def parse_args():
    parser = argparse.ArgumentParser(description="Simple thermal UDP viewer")
    parser.add_argument("--bind", default=DEFAULT_BIND)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    return parser.parse_args()


def main():
    args = parse_args()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.bind, args.port))
    sock.settimeout(1.0)

    assembler = FrameAssembler()
    log(f"listening on udp://{args.bind}:{args.port}")

    try:
        while True:
            try:
                payload, addr = sock.recvfrom(4096)
            except socket.timeout:
                continue

            result = assembler.push(payload)
            if result is None:
                continue

            frame_id, min_val, max_val, frame_bytes, bytes_per_pixel = result
            log(f"frame complete: id={frame_id} from={addr[0]}:{addr[1]}")
            render_frame(assembler, frame_id, min_val, max_val, frame_bytes, bytes_per_pixel)
    except KeyboardInterrupt:
        log("stopped")
    finally:
        sock.close()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
