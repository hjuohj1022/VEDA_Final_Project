# ESP32 ESP-IDF Build Guide

This project is now laid out in the standard ESP-IDF style:

- top-level `CMakeLists.txt`
- application sources in `main/`
- project configuration in `sdkconfig`

## Board

- Target: `esp32c5`
- Reference board: `esp32-c5` development board

## Local secrets

Create a local secrets header before running the firmware:

1. Copy `main/app_secrets.example.h` to `main/app_secrets.h`
2. Fill in your Wi-Fi and MQTT values

Example:

```c
#ifndef APP_SECRETS_H
#define APP_SECRETS_H

#define APP_WIFI_SSID "your-ssid"
#define APP_WIFI_PASS "your-password"
#define APP_MQTT_BROKER_URI "mqtts://your-broker-host:8883"

#endif
```

`main/app_secrets.h` is ignored by Git.

For the helper Python client:

1. Copy `main/mqtt/app_client_config.example.py` to `main/mqtt/app_client_config.py`
2. Fill in the MQTT settings

Example:

```python
MQTT_BROKER = "your-broker-host"
MQTT_PORT = 8883
MQTT_TLS_INSECURE = True
```

`main/mqtt/app_client_config.py` is ignored by Git.

## Build

```powershell
idf.py set-target esp32c5
idf.py build
```

## Flash and monitor

```powershell
idf.py -p COMx flash
idf.py -p COMx monitor
```

Or:

```powershell
idf.py -p COMx flash monitor
```

## Embedded certificates

Certificates are embedded from `main/CMakeLists.txt`:

- `main/certs/rootCA.crt`
- `main/certs/client-stm32.crt`
- `main/certs/client-stm32.key`

## Notes

- `sdkconfig` currently targets `esp32c5`
- local `sdkconfig` is generated and updated by ESP-IDF
