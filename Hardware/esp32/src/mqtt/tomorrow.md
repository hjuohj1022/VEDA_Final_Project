# Tomorrow's Plan (2026-03-05)

## 1. Current Progress Summary
- **Issue:** Frequent MQTT disconnections and `esp-tls: Failed to open new connection` timeouts.
- **Root Cause:**
  - Extremely low WiFi RSSI (-86dBm to -88dBm) causing packet loss during TLS handshakes.
  - High CPU/Memory overhead from MQTTS(TLS) during high-throughput (38.4KB/s) transmission.
- **Teensy Status:** 1-second delay (`delay(1000)`) added to `testflir.ino` (1fps) to prevent overwhelming ESP32.

## 2. To-Do Tomorrow
1. **Switch to Non-Secure MQTT:** Change `mqtts://` (8883) to `mqtt://` (1883) in `mqtt.c` to reduce overhead.
2. **WiFi Environment Improvement:** Improve the physical environment to achieve RSSI above -70dBm.
3. **Verify Broker Port:** Ensure the MQTT broker (192.168.55.200) has port 1883 open for non-secure connections.

## 3. Reference Settings (Non-secure)
```c
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri              = "mqtt://192.168.55.200:1883",
        .buffer.size                     = 2048,   
        .buffer.out_size                 = 8192,   
        .network.timeout_ms              = 10000,
        .network.reconnect_timeout_ms    = 5000,   
        .session.keepalive               = 30,     
    };
```
