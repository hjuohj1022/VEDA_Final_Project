import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion
import numpy as np
import cv2
import struct
import ssl
import os

# --- 설정 ---
MQTT_BROKER = "192.168.55.200"  # 브로커 IP
MQTT_PORT = 8883               # MQTTS 보안 포트
MQTT_TOPIC = "lepton/frame/chunk"

# 인증서 경로 (현재 파일 기준 상대 경로)
CERT_DIR = os.path.join(os.path.dirname(__file__), "..", "certs")
CA_CERT     = os.path.join(CERT_DIR, "ca_cert.pem")
CLIENT_CERT = os.path.join(CERT_DIR, "client_cert.pem")
CLIENT_KEY  = os.path.join(CERT_DIR, "client_key.pem")

# Lepton 3.5 사양 (160x120)
WIDTH = 160
HEIGHT = 120
FRAME_BYTES = WIDTH * HEIGHT * 2  # 38,400 bytes

import time

# 전역 변수
current_frame_chunks = {}
total_chunks_expected = 0

def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print("✅ MQTTS Connected Successfully")
        client.subscribe(MQTT_TOPIC)
        print(f"📡 Subscribed to {MQTT_TOPIC}, waiting for data...")
    else:
        print(f"❌ Connection failed with code {rc}")

def on_disconnect(client, userdata, flags, rc, properties=None):
    print(f"📡 Disconnected with result code {rc}. Reconnecting...")

def on_message(client, userdata, msg):
    global current_frame_chunks, total_chunks_expected

    # 1. 헤더 파싱 (8바이트: index(2), total(2), min(2), max(2))
    header_size = 8
    if len(msg.payload) < header_size:
        print(f"⚠️ Small payload: {len(msg.payload)}")
        return

    header = msg.payload[:header_size]
    idx, total, min_val, max_val = struct.unpack(">HHHH", header)
    payload = msg.payload[header_size:]
    
    # 2. 청크 수집 및 유효성 검사
    if total <= 0 or total > 100: # 이상 데이터 방지
        return

    # 새로운 프레임 시작 감지 (첫 번째 청크이거나 예상 개수가 바뀐 경우)
    if total != total_chunks_expected:
        current_frame_chunks = {}
        total_chunks_expected = total
    
    current_frame_chunks[idx] = payload
    
    # 디버깅 출력
    if idx % 5 == 0 or idx == total - 1:
        print(f"📦 [{idx+1}/{total}] Min:{min_val} Max:{max_val} Data:{len(payload)} bytes")

    # 3. 모든 청크가 모였는지 확인
    if len(current_frame_chunks) == total_chunks_expected:
        # 데이터 복사 후 버퍼 즉시 초기화 (다음 프레임 수신 준비)
        frame_data_copy = dict(current_frame_chunks)
        current_frame_chunks = {}
        
        print("🖼️ Frame complete! Processing...")
        assemble_and_display(frame_data_copy, total_chunks_expected, min_val, max_val)

def assemble_and_display(chunks, total, min_val, max_val):
    # 순서대로 합치기
    full_data = bytearray()
    for i in range(total):
        if i in chunks:
            full_data.extend(chunks[i])
        else:
            print(f"⚠️ Missing chunk {i}, dropping frame")
            return

    if len(full_data) < FRAME_BYTES:
        print(f"⚠️ Incomplete data: {len(full_data)} / {FRAME_BYTES}")
        return

    # 4. RAW 데이터를 numpy 배열로 변환 (Big-Endian 16bit)
    raw_frame = np.frombuffer(full_data[:FRAME_BYTES], dtype='>u2').reshape((HEIGHT, WIDTH))
    
    # 0~65535 범위에서 Lepton 데이터는 보통 7000~10000 근처임
    # 1000 이하(에러) 및 30000 이상(노이즈)을 제외한 유효 데이터 추출
    valid_mask = (raw_frame > 1000) & (raw_frame < 30000)
    valid_pixels = raw_frame[valid_mask]
    
    if len(valid_pixels) > 0:
        # 상하위 2%를 버리고 96% 구간에 집중하여 대비 향상 (Percentile)
        f_min = np.percentile(valid_pixels, 2)
        f_max = np.percentile(valid_pixels, 98)
        
        # 최소/최대 차이가 너무 적으면 노이즈가 강조되므로 최소 범위 보장
        if f_max - f_min < 100:
            f_max = f_min + 100
    else:
        f_min, f_max = min_val, max_val

    # 5. 명암비 조절 (Normalization)
    if f_max > f_min:
        display_frame = (raw_frame.astype(np.float32) - f_min) / (f_max - f_min) * 255
        display_frame = np.clip(display_frame, 0, 255).astype(np.uint8)
    else:
        display_frame = np.zeros((HEIGHT, WIDTH), dtype=np.uint8)

    # 6. 컬러맵 적용
    color_frame = cv2.applyColorMap(display_frame, cv2.COLORMAP_JET)

    # 7. 화면 출력 및 확대
    resized_frame = cv2.resize(color_frame, (800, 600), interpolation=cv2.INTER_LINEAR)
    
    # 정보 표시 (디버깅 - 터미널에도 출력)
    info_text = f"Min:{int(f_min)} Max:{int(f_max)} (Percentile)"
    print(f"📊 Display Range: {int(f_min)} ~ {int(f_max)}")
    
    cv2.putText(resized_frame, info_text, (20, 40), 
                cv2.FONT_HERSHEY_DUPLEX, 0.8, (255, 255, 255), 1)
    
    cv2.imshow("Thermal Stream (Secure)", resized_frame)
    cv2.waitKey(1) # 아주 짧은 대기로 화면 갱신 보장


# --- MQTT 클라이언트 설정 ---
# 최신 paho-mqtt 라이브러리 경고 해결 (VERSION2 사용)
client = mqtt.Client(callback_api_version=CallbackAPIVersion.VERSION2)
client.on_connect = on_connect
client.on_message = on_message
client.on_disconnect = on_disconnect

# TLS 설정 (MQTTS)
try:
    client.tls_set(
        ca_certs=CA_CERT,
        certfile=CLIENT_CERT,
        keyfile=CLIENT_KEY,
        cert_reqs=ssl.CERT_REQUIRED,
        tls_version=ssl.PROTOCOL_TLSv1_2
    )
    client.tls_insecure_set(True) 
except Exception as e:
    print(f"❌ TLS Setup Error: {e}")
    exit()

print(f"Connecting to MQTTS Broker at {MQTT_BROKER}:{MQTT_PORT}...")

# 초기 연결 시도 루프
connected = False
while not connected:
    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        connected = True
    except Exception as e:
        print(f"❌ Initial connection failed: {e}. Retrying in 5 seconds...")
        time.sleep(5)

try:
    client.loop_forever()
except KeyboardInterrupt:
    print("\nStopped by user")
finally:
    cv2.destroyAllWindows()
