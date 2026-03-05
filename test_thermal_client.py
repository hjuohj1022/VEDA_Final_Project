import websocket
import numpy as np
import cv2
import sys
import ssl
import os

# ---------------------------------------------------------
# 서버 및 인증서 설정
# ---------------------------------------------------------
SERVER_IP = "192.168.55.200" 
WS_URL = f"wss://{SERVER_IP}/thermal-stream"

# 인증서 경로
CERT_DIR = os.path.join(os.path.dirname(__file__), "RaspberryPi", "k3s-cluster", "security", "certs")
CLIENT_CRT = os.path.join(CERT_DIR, "client-qt.crt")
CLIENT_KEY = os.path.join(CERT_DIR, "client-qt.key")
ROOT_CA    = os.path.join(CERT_DIR, "rootCA.crt")

# ---------------------------------------------------------
# 설정 및 전역 변수
# ---------------------------------------------------------
TOTAL_CHUNKS = 38
EXPECTED_SIZE = 38400  # 160x120x2 bytes (16-bit pixels)
received_chunks = {}
window_name = "Thermal Stream (Auto-Reassembled)"

# 초기 화면 구성을 위한 빈 이미지 (640x480)
current_display_frame = np.zeros((480, 640, 3), dtype=np.uint8)
cv2.putText(current_display_frame, "Waiting for data...", (160, 240), 
            cv2.FONT_HERSHEY_SIMPLEX, 1, (255, 255, 255), 2)

def show_window():
    """현재 프레임을 화면에 표시"""
    cv2.imshow(window_name, current_display_frame)
    cv2.waitKey(1)

def process_frame_data(full_frame_bytes):
    """바이트 데이터를 160x120 이미지로 변환 및 컬러맵 적용"""
    global current_display_frame
    try:
        # 16비트 정수로 해석 (각 픽셀 2바이트)
        final_data = np.frombuffer(full_frame_bytes[:EXPECTED_SIZE], dtype=np.uint16)
        
        if len(final_data) == 160 * 120:
            frame = final_data.reshape((120, 160))
            
            # 시각화 가공 (정규화 및 컬러맵)
            # 14-bit 데이터를 8-bit로 정규화
            frame_norm = cv2.normalize(frame, None, 0, 255, cv2.NORM_MINMAX, cv2.CV_8U)
            frame_color = cv2.applyColorMap(frame_norm, cv2.COLORMAP_JET)
            
            # 640x480으로 확대
            current_display_frame = cv2.resize(frame_color, (640, 480), interpolation=cv2.INTER_CUBIC)
            return True
        else:
            print(f"Data size mismatch: {len(final_data)} pixels")
    except Exception as e:
        print(f"Process Error: {e}")
    return False

def on_message(ws, message):
    global received_chunks
    
    if isinstance(message, str):
        # 텍스트 메시지는 무시하거나 로깅
        return

    msg_len = len(message)

    # 1. 만약 데이터가 한 번에 전체 프레임(38400)으로 들어온 경우 바로 처리
    if msg_len == EXPECTED_SIZE:
        if process_frame_data(message):
            show_window()
        return

    # 2. 조각(Chunk) 데이터 처리 로직 (앞 4바이트가 [idx(2), total(2)] 헤더인 경우)
    if msg_len < 5:
        return

    try:
        # 새로운 헤더 형식: index(2B) + total_chunks(2B) (Big Endian)
        idx = int.from_bytes(message[0:2], byteorder='big')
        total_chunks = int.from_bytes(message[2:4], byteorder='big')
        payload = message[4:]
        
        # 인덱스가 유효한 범위 내인지 확인 (비정상 데이터 방어)
        if idx < total_chunks: 
            received_chunks[idx] = payload
            
            # 모든 조각이 모였는지 확인
            if len(received_chunks) >= total_chunks:
                full_frame_data = b""
                is_complete = True
                for i in range(total_chunks):
                    if i in received_chunks:
                        full_frame_data += received_chunks[i]
                    else:
                        is_complete = False
                        break
                
                if is_complete:
                    # 충분한 데이터가 모였는지 확인 (FRAME_BYTES=38400)
                    if len(full_frame_data) >= EXPECTED_SIZE:
                        process_frame_data(full_frame_data)
                        received_chunks.clear() # 성공 시 버퍼 초기화
                        show_window()
        else:
            # 인덱스가 비정상적으로 크면 인덱스가 없는 순수 데이터 프레임일 수 있음
            if msg_len >= EXPECTED_SIZE:
                if process_frame_data(message):
                    show_window()

    except Exception as e:
        print(f"Process Error: {e}")

def on_error(ws, error):
    print(f"WebSocket Error: {error}")

def on_close(ws, close_status_code, close_msg):
    print(f"### Connection Closed ###")

def on_open(ws):
    print(f"### Connected to Secure Thermal Stream Server ###")
    # 연결 성공 시 즉시 빈 화면 갱신
    show_window()

if __name__ == "__main__":
    # 인증서 확인
    for f in [CLIENT_CRT, CLIENT_KEY]:
        if not os.path.exists(f):
            print(f"Error: Certificate file not found: {f}")
            sys.exit(1)

    # 초기 윈도우 생성 및 대기 화면 표시
    cv2.namedWindow(window_name, cv2.WINDOW_NORMAL)
    show_window()

    # SSL 설정 (mTLS 인증 및 자체서명 우회)
    ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ssl_context.load_cert_chain(certfile=CLIENT_CRT, keyfile=CLIENT_KEY)
    ssl_context.check_hostname = False
    ssl_context.verify_mode = ssl.CERT_NONE

    ws = websocket.WebSocketApp(WS_URL,
                              on_open=on_open,
                              on_message=on_message,
                              on_error=on_error,
                              on_close=on_close)
    
    try:
        print(f"Starting Thermal Stream Client (Target: {SERVER_IP})...")
        # WebSocket 실행 (메시지 수신 시마다 on_message가 호출됨)
        ws.run_forever(sslopt={"context": ssl_context})
    except KeyboardInterrupt:
        print("\nExit.")
        cv2.destroyAllWindows()
        sys.exit(0)
