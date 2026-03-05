import paho.mqtt.client as mqtt
import ssl
import os
import numpy as np
import cv2
import sys

# ---------------------------------------------------------
# 서버 및 인증서 설정
# ---------------------------------------------------------
SERVER_IP = "192.168.55.200"
MQTT_PORT = 8883
TOPIC = "lepton/frame/chunk"

# 인증서 경로 (RaspberryPi/k3s-cluster/security/certs/ 아래에 있는 것을 기준으로 설정)
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CERT_DIR = os.path.join(BASE_DIR, "RaspberryPi", "k3s-cluster", "security", "certs")
CA_CERT = os.path.join(CERT_DIR, "rootCA.crt")
CLIENT_CERT = os.path.join(CERT_DIR, "client-qt.crt")
CLIENT_KEY = os.path.join(CERT_DIR, "client-qt.key")

# ---------------------------------------------------------
# 열화상 프레임 재조합 변수
# ---------------------------------------------------------
# Lepton 3.5: 160x120 pixels, 각 픽셀 2바이트 (uint16) -> 총 38400 bytes
FRAME_WIDTH = 160
FRAME_HEIGHT = 120
EXPECTED_FRAME_SIZE = FRAME_WIDTH * FRAME_HEIGHT * 2 

received_chunks = {} # {chunk_index: payload}
current_total_chunks = 0

def process_and_display(full_data):
    """바이트 데이터를 numpy 배열로 변환하여 화면에 출력"""
    try:
        # uint16 (Big Endian or Little Endian depending on Teensy code)
        # Teensy: ((uint16_t)rawPacket[4 + i * 2] << 8) | rawPacket[5 + i * 2] -> Big Endian 저장
        
        # 38400 바이트가 넘을 수 있으므로 정확히 슬라이싱
        final_data = np.frombuffer(full_data[:EXPECTED_FRAME_SIZE], dtype=np.uint16)
        
        if len(final_data) == FRAME_WIDTH * FRAME_HEIGHT:
            frame = final_data.reshape((FRAME_HEIGHT, FRAME_WIDTH))
            
            # 1. 정규화 (0~65535 -> 0~255)
            # 14-bit 데이터이므로 보통 0~16383 범위임
            frame_norm = cv2.normalize(frame, None, 0, 255, cv2.NORM_MINMAX, cv2.CV_8U)
            
            # 2. 컬러맵 적용 (열화상 느낌을 위해 JET)
            frame_color = cv2.applyColorMap(frame_norm, cv2.COLORMAP_JET)
            
            # 3. 화면 표시를 위해 확대 (640x480)
            frame_resized = cv2.resize(frame_color, (640, 480), interpolation=cv2.INTER_CUBIC)
            
            # 텍스트 오버레이
            cv2.putText(frame_resized, "MQTTS Thermal Stream", (10, 30), 
                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, (255, 255, 255), 2)
            
            cv2.imshow("Thermal Camera (MQTTS)", frame_resized)
            cv2.waitKey(1)
        else:
            print(f"Data size error: expected {FRAME_WIDTH*FRAME_HEIGHT}, got {len(final_data)}")
            
    except Exception as e:
        print(f"Display Error: {e}")

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"Connected to MQTTS Broker ({SERVER_IP}:{MQTT_PORT})")
        client.subscribe(TOPIC)
        print(f"Subscribed to topic: {TOPIC}")
    else:
        print(f"Connection failed with code {rc}")

def on_message(client, userdata, msg):
    global received_chunks, current_total_chunks
    
    data = msg.payload
    if len(data) < 4:
        return
        
    # 헤더 파싱 (Big Endian)
    # data[0..1]: chunk_index
    # data[2..3]: total_chunks
    chunk_index = (data[0] << 8) | data[1]
    total_chunks = (data[2] << 8) | data[3]
    payload = data[4:]
    
    received_chunks[chunk_index] = payload
    current_total_chunks = total_chunks

    # 모든 조각이 모였는지 확인
    if len(received_chunks) >= total_chunks:
        is_complete = True
        full_frame_data = b""
        for i in range(total_chunks):
            if i in received_chunks:
                full_frame_data += received_chunks[i]
            else:
                is_complete = False
                break
        
        if is_complete:
            if len(full_frame_data) >= EXPECTED_FRAME_SIZE:
                process_and_display(full_frame_data)
                received_chunks.clear()

def on_error(client, userdata, level, string):
    print(f"MQTT Error: {string}")

if __name__ == "__main__":
    # 인증서 파일 존재 확인
    missing_certs = []
    for f in [CA_CERT, CLIENT_CERT, CLIENT_KEY]:
        if not os.path.exists(f):
            missing_certs.append(f)
    
    if missing_certs:
        print("Error: Missing certificate files:")
        for mc in missing_certs:
            print(f" - {mc}")
        sys.exit(1)

    # MQTT 클라이언트 초기화
    client = mqtt.Client()
    
    # SSL/TLS 설정 (mTLS)
    try:
        client.tls_set(
            ca_certs=CA_CERT,
            certfile=CLIENT_CERT,
            keyfile=CLIENT_KEY,
            cert_reqs=ssl.CERT_NONE
        )
        client.tls_insecure_set(True)
    except Exception as e:
        print(f"TLS Setup Error: {e}")
        sys.exit(1)

    client.on_connect = on_connect
    client.on_message = on_message
    
    print(f"Connecting to MQTTS {SERVER_IP}...")
    try:
        client.connect(SERVER_IP, MQTT_PORT, keepalive=60)
        client.loop_forever()
    except KeyboardInterrupt:
        print("\nDisconnected.")
        cv2.destroyAllWindows()
        sys.exit(0)
    except Exception as e:
        print(f"Runtime Error: {e}")
        sys.exit(1)
