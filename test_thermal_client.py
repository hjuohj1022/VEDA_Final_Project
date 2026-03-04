import websocket
import numpy as np
import cv2
import sys
import ssl
import os

# ---------------------------------------------------------
# 서버 및 인증서 설정
# ---------------------------------------------------------
# 1. 서버 주소 (라즈베리파이 IP 또는 도메인으로 수정하세요)
SERVER_IP = "192.168.55.200" 
WS_URL = f"wss://{SERVER_IP}/thermal-stream"

# 2. 인증서 경로 (RaspberryPi/k3s-cluster/security/certs 내 파일들)
# 현재 스크립트 위치 기준으로 상대 경로 설정 (필요시 절대 경로로 수정)
CERT_DIR = os.path.join(os.path.dirname(__file__), "RaspberryPi", "k3s-cluster", "security", "certs")
CLIENT_CRT = os.path.join(CERT_DIR, "client-qt.crt")
CLIENT_KEY = os.path.join(CERT_DIR, "client-qt.key")
ROOT_CA    = os.path.join(CERT_DIR, "rootCA.crt")

def on_message(ws, message):
    if isinstance(message, str):
        # 텍스트 메시지는 무시하거나 로깅
        return

    # 바이너리 데이터 수신 (38400 바이트 예상: 160*120*2)
    data = np.frombuffer(message, dtype=np.uint16)
    
    if len(data) == 160 * 120:
        frame = data.reshape((120, 160))
        
        # 시각화 최적화 (0~255 정규화 및 Jet 컬러맵)
        frame_norm = cv2.normalize(frame, None, 0, 255, cv2.NORM_MINMAX, cv2.CV_8U)
        frame_color = cv2.applyColorMap(frame_norm, cv2.COLORMAP_JET)
        
        # 640x480으로 확대해서 표시
        frame_resized = cv2.resize(frame_color, (640, 480), interpolation=cv2.INTER_CUBIC)
        
        cv2.imshow("Thermal Stream (mTLS)", frame_resized)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            ws.close()
    else:
        print(f"Received unexpected data size: {len(message)} bytes")

def on_error(ws, error):
    print(f"Error: {error}")

def on_close(ws, close_status_code, close_msg):
    print(f"### Connection Closed: {close_status_code} - {close_msg} ###")

def on_open(ws):
    print(f"### Connected to Secure Thermal Stream Server: {WS_URL} ###")

if __name__ == "__main__":
    # 인증서 파일 존재 확인
    for f in [CLIENT_CRT, CLIENT_KEY, ROOT_CA]:
        if not os.path.exists(f):
            print(f"Error: Certificate file not found: {f}")
            sys.exit(1)

    print(f"Connecting to {WS_URL} using mTLS...")

    # SSL 컨텍스트 설정 (mTLS)
    ssl_context = ssl.create_default_context(ssl.Purpose.SERVER_AUTH, cafile=ROOT_CA)
    ssl_context.load_cert_chain(certfile=CLIENT_CRT, keyfile=CLIENT_KEY)
    
    # 자체 서명 인증서를 사용하는 경우 호스트네임 검증을 끄려면 아래 주석 해제 (테스트 전용)
    # ssl_context.check_hostname = False
    # ssl_context.verify_mode = ssl.CERT_NONE

    ws = websocket.WebSocketApp(WS_URL,
                              on_open=on_open,
                              on_message=on_message,
                              on_error=on_error,
                              on_close=on_close)
    
    try:
        ws.run_forever(sslopt={"context": ssl_context})
    except KeyboardInterrupt:
        print("Interrupted by user")
        sys.exit(0)
