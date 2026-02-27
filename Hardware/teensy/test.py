import serial
import numpy as np
import cv2
import time
import struct

# 프로토콜: FSTART(6) + offset_byte(1) + frame_data(38400)
FRAME_BYTES  = 160 * 120 * 2   # 38400
HEADER       = b"FSTART"
HEADER_LEN   = len(HEADER)
PROTO_EXTRA  = 1                # offset 바이트 1개
TOTAL_PKT    = HEADER_LEN + PROTO_EXTRA + FRAME_BYTES

PIXELS_PER_PKT  = 80
PACKETS_PER_SEG = 60

# ── RAW14 → 온도 변환 (선형 보정) ──
# 현재 센서 출력 범위 기준 (실측값 보고 RAW_MIN/MAX 조정)
RAW_MIN,  RAW_MAX  = 7800, 8500
TEMP_MIN, TEMP_MAX = 15.0, 45.0

def raw14_to_celsius(raw_val):
    ratio = (raw_val - RAW_MIN) / (RAW_MAX - RAW_MIN)
    celsius = TEMP_MIN + ratio * (TEMP_MAX - TEMP_MIN)
    return round(float(np.clip(celsius, TEMP_MIN - 20, TEMP_MAX + 20)), 1)

ser = serial.Serial('COM6', 2000000, timeout=0)
raw_buffer  = b""
frame_count = 0
last_print  = time.time()
debug_mode  = True

print("Lepton 3.0 — 'q' quit  'd' debug toggle")
print(f"온도 매핑: RAW {RAW_MIN}~{RAW_MAX} → {TEMP_MIN}~{TEMP_MAX}°C")

while True:
    waiting = ser.in_waiting
    if waiting > 0:
        raw_buffer += ser.read(waiting)

    if len(raw_buffer) > TOTAL_PKT * 10:
        last_idx = raw_buffer.rfind(HEADER)
        raw_buffer = raw_buffer[last_idx:] if last_idx != -1 else b""

    while True:
        start_idx = raw_buffer.find(HEADER)
        if start_idx == -1:
            break

        pkt_end = start_idx + TOTAL_PKT
        if len(raw_buffer) < pkt_end:
            break

        # 다음 헤더가 프레임 내부에 있으면 깨진 프레임 → 건너뜀
        next_start = raw_buffer.find(HEADER, start_idx + HEADER_LEN)
        if next_start != -1 and next_start < pkt_end:
            raw_buffer = raw_buffer[next_start:]
            continue

        # offset 바이트 읽기 (= Arduino가 저장 시작한 packetId, 보통 20)
        offset_packet = raw_buffer[start_idx + HEADER_LEN]
        frame_data    = raw_buffer[start_idx + HEADER_LEN + PROTO_EXTRA : pkt_end]
        raw_buffer    = raw_buffer[pkt_end:]

        try:
            frame = np.frombuffer(frame_data, dtype=np.uint16).reshape(120, 160).copy()

            # ── seg1의 packet 0~19가 0으로 채워진 경우 보정 ──
            missing_rows = offset_packet // 2
            if missing_rows > 0 and missing_rows < 120:
                frame[:missing_rows, :] = frame[missing_rows, :]

            # ── 최고온도 좌표 탐색 (클리핑 전 원본에서) ──
            hot_idx = np.argmax(frame)
            hot_y, hot_x = np.unravel_index(hot_idx, frame.shape)
            hot_val     = int(frame[hot_y, hot_x])
            hot_celsius = raw14_to_celsius(hot_val)

            # 픽셀값 통계
            frame_count += 1
            now = time.time()
            if debug_mode and now - last_print >= 1.0:
                last_print = now
                mn, mx = frame.min(), frame.max()
                p1, p99 = np.percentile(frame, 1), np.percentile(frame, 99)
                print(f"[{frame_count}] min={mn} max={mx} p1={p1:.0f} p99={p99:.0f} "
                      f"offset_pkt={offset_packet} missing_rows={missing_rows} "
                      f"HOT=({hot_x},{hot_y}) val={hot_val} temp={hot_celsius:.1f}°C")

            # 정규화 및 컬러맵
            p2, p98 = np.percentile(frame, 2), np.percentile(frame, 98)
            frame_clipped = np.clip(frame, p2, p98)
            img_norm  = cv2.normalize(frame_clipped, None, 0, 255,
                                      cv2.NORM_MINMAX, dtype=cv2.CV_8U)
            img_norm  = cv2.medianBlur(img_norm, 3)
            color_img = cv2.applyColorMap(img_norm, cv2.COLORMAP_INFERNO)

            if debug_mode:
                mn, mx = int(frame.min()), int(frame.max())
                cv2.putText(color_img, f"min:{mn} max:{mx} offset_pkt:{offset_packet}",
                            (5, 15), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255,255,255), 1)

            # ── 최고온도 위치 표시 (640x480 스케일 변환) ──
            scale_x, scale_y = 640 / 160, 480 / 120
            disp_x = int(hot_x * scale_x)
            disp_y = int(hot_y * scale_y)

            resized = cv2.resize(color_img, (640, 480), interpolation=cv2.INTER_CUBIC)

            cv2.drawMarker(resized, (disp_x, disp_y),
                           (0, 255, 0), cv2.MARKER_CROSS, 15, 2)
            cv2.putText(resized,
                        f"HOT ({hot_x},{hot_y}) {hot_celsius:.1f}C",
                        (disp_x + 8, disp_y - 8),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 255, 0), 1)

            cv2.imshow('Lepton 3.0', resized)

        except Exception as e:
            print(f"Error: {e}")

    key = cv2.waitKey(1) & 0xFF
    if key == ord('q'):
        break
    elif key == ord('d'):
        debug_mode = not debug_mode
        print(f"Debug: {'ON' if debug_mode else 'OFF'}")

ser.close()
cv2.destroyAllWindows()