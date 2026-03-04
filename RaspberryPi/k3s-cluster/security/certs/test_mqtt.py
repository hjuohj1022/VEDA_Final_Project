import paho.mqtt.client as mqtt
import ssl

def on_connect(client, userdata, flags, rc):
    print(f"Connected: {rc}")
    client.subscribe("lepton/frame/chunk")

def on_message(client, userdata, msg):
    data = msg.payload
    chunk_index  = (data[0] << 8) | data[1]
    total_chunks = (data[2] << 8) | data[3]
    print(f"Chunk {chunk_index}/{total_chunks} ({len(data)-4} bytes)")

client = mqtt.Client()
client.tls_set(
    ca_certs="rootCA.crt",
    certfile="client-qt.crt",
    keyfile="client-qt.key",
    cert_reqs=ssl.CERT_NONE
)

client.tls_insecure_set(True)

client.on_connect = on_connect
client.on_message = on_message
client.connect("192.168.55.200", 8883)
client.loop_forever()