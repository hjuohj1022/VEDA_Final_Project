import paho.mqtt.client as mqtt
import numpy as np
import cv2

TOPIC = "lepton/frame/full"
FRAME_WIDTH, FRAME_HEIGHT = 160, 120

def on_message(client, userdata, msg):
    # 38,400 바이트가 한 번에 들어옴
    data = msg.payload
    if len(data) != FRAME_WIDTH * FRAME_HEIGHT * 2:
        print(f"Invalid frame size: {len(data)}")
        return

    # 이미지 변환 및 출력
    raw_data = np.frombuffer(data, dtype=np.uint16)
    img = raw_data.reshape((FRAME_HEIGHT, FRAME_WIDTH))
    img_norm = cv2.normalize(img, None, 0, 255, cv2.NORM_MINMAX, cv2.CV_8U)
    img_color = cv2.applyColorMap(img_norm, cv2.COLORMAP_JET)
    img_resized = cv2.resize(img_color, (640, 480))

    cv2.imshow("Lepton Full Frame", img_resized)
    cv2.waitKey(1)

client = mqtt.Client()
client.on_message = on_message
client.connect("192.168.55.200", 1883)
client.subscribe(TOPIC)
client.loop_forever()