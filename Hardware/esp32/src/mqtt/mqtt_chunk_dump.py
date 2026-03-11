import argparse
import os
import ssl
import struct
import time

import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion


def log(message):
    print(message, flush=True)


def parse_args():
    parser = argparse.ArgumentParser(description="Dump MQTT thermal chunks")
    parser.add_argument("--host", default="192.168.55.200")
    parser.add_argument("--port", type=int, default=8883)
    parser.add_argument("--topic", default="lepton/#")
    parser.add_argument("--tls", action="store_true", default=True)
    parser.add_argument("--no-tls", dest="tls", action="store_false")
    parser.add_argument("--insecure", action="store_true", default=True)
    parser.add_argument("--secure-cn", dest="insecure", action="store_false")
    return parser.parse_args()


def main():
    args = parse_args()
    cert_dir = os.path.join(os.path.dirname(__file__), "..", "certs")
    ca_cert = os.path.join(cert_dir, "rootCA.crt")
    client_cert = os.path.join(cert_dir, "client-stm32.crt")
    client_key = os.path.join(cert_dir, "client-stm32.key")

    msg_count = 0
    frame_chunk_count = 0

    client = mqtt.Client(callback_api_version=CallbackAPIVersion.VERSION2)

    def on_connect(cli, userdata, flags, rc, properties=None):
        del userdata, flags, properties
        if rc == 0:
            log(f"connected to {args.host}:{args.port}")
            cli.subscribe(args.topic)
            log(f"subscribed: {args.topic}")
        else:
            log(f"connect failed: rc={rc}")

    def on_disconnect(cli, userdata, flags, rc, properties=None):
        del cli, userdata, flags, properties
        log(f"disconnected: rc={rc}")

    def on_message(cli, userdata, msg):
        nonlocal msg_count, frame_chunk_count
        del cli, userdata
        msg_count += 1
        if (msg.topic == "lepton/frame/chunk") and (len(msg.payload) >= 10):
            frame_chunk_count += 1
            frame_id, idx, total, min_val, max_val = struct.unpack(">HHHHH", msg.payload[:10])
            log(
                f"msg#{msg_count} chunk#{frame_chunk_count} topic={msg.topic} "
                f"frame={frame_id} idx={idx}/{total - 1} min={min_val} max={max_val} "
                f"payload_len={len(msg.payload)}"
            )
        else:
            log(f"msg#{msg_count} topic={msg.topic} payload_len={len(msg.payload)}")

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

    log("mqtt_chunk_dump.py start")
    log(f"connecting to {args.host}:{args.port} topic={args.topic} tls={args.tls} insecure={args.insecure}")
    client.connect(args.host, args.port, 60)
    client.loop_forever()


if __name__ == "__main__":
    main()
