import paho.mqtt.client as mqtt
import ssl
import time
import os

# ================================
# 설정 정보
# ================================
MQTT_HOST = "192.168.55.200"
MQTT_PORT = 8883
TOPIC = "system/status"
MESSAGE = "Hello from Python Test Script (MQTTS)"
CLIENT_ID = "python_test_client_001"

# 인증서 경로
CA_CERT = "Qt_Client/certs/rootCA.crt"
CLIENT_CERT = "Qt_Client/certs/client-qt.crt"
CLIENT_KEY = "Qt_Client/certs/client-qt.key"

def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print(f"✅ 연결 성공 (결과 코드: {rc})")
        client.publish(TOPIC, MESSAGE)
        print(f"📤 메시지 전송 완료: {TOPIC} -> {MESSAGE}")
    else:
        print(f"❌ 연결 실패 (결과 코드: {rc})")

def on_publish(client, userdata, mid, reason_code=None, properties=None):
    print("✨ 발행 확인 완료. 프로그램을 종료합니다.")
    client.disconnect()

def on_log(client, userdata, level, buf):
    print(f"🔍 [LOG] {buf}")

# 1. 파일 존재 확인
for f in [CA_CERT, CLIENT_CERT, CLIENT_KEY]:
    if not os.path.exists(f):
        print(f"🚨 파일이 존재하지 않습니다: {f}")

# 2. 클라이언트 생성 (최신 API 버전 2)
client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2, client_id=CLIENT_ID)
client.on_connect = on_connect
client.on_publish = on_publish
client.on_log = on_log

# 3. SSL/TLS 설정
try:
    print(f"🔄 {MQTT_HOST}:{MQTT_PORT}에 보안 연결 시도 중...")
    client.tls_set(
        ca_certs=CA_CERT,
        certfile=CLIENT_CERT,
        keyfile=CLIENT_KEY,
        tls_version=ssl.PROTOCOL_TLSv1_2
    )
    client.tls_insecure_set(True)

    client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
    client.loop_forever()

except Exception as e:
    print(f"🚨 오류 발생: {e}")
