# ESP32 Build Guide

This folder is an ESP-IDF project for `esp32c5`.

It is not a project for `STMicroelectronics.stm32-vscode-extension`.

## Project layout

- Root CMake file: `CMakeLists.txt`
- App component: `main/`
- Target: `esp32c5`
- Default serial port in workspace settings: `COM9`

## Prerequisites

Installed paths used by this project:

- ESP-IDF: `C:\esp\v5.5.3\esp-idf`
- ESP-IDF tools: `C:\Espressif`
- Python env: `C:\Espressif\tools\python\v5.5.3\venv`

## Terminal build

Open PowerShell in this folder and run:

```powershell
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PYTHON_ENV_PATH='C:\Espressif\tools\python\v5.5.3\venv'
$env:IDF_PYTHON_CHECK_CONSTRAINTS='no'
$env:PATH='C:\Espressif\tools\python\v5.5.3\venv\Scripts;'+$env:PATH
& 'C:\esp\v5.5.3\esp-idf\export.ps1'
idf.py set-target esp32c5
idf.py build
```

If `sdkconfig` already targets `esp32c5`, `idf.py set-target esp32c5` can be skipped.

## Flash

```powershell
idf.py -p COM9 flash
```

## Monitor

```powershell
idf.py -p COM9 monitor
```

## Flash and monitor together

```powershell
idf.py -p COM9 flash monitor
```

## VS Code build

Use the `ESP-IDF` extension, not the STM32 extension.

Workspace settings are already prepared in `.vscode/settings.json`.

Recommended order:

1. Open this `esp32` folder in VS Code.
2. Run `Developer: Reload Window`.
3. Run `ESP-IDF: Select Current ESP-IDF Version` and confirm `C:\esp\v5.5.3\esp-idf`.
4. Run `ESP-IDF: Set Espressif Device Target` and confirm `esp32c5`.
5. Run `ESP-IDF: Select Port to Use` and confirm `COM9`.
6. Run `ESP-IDF: Build your project`.

Useful ESP-IDF commands:

- Build: `ESP-IDF: Build your project`
- Flash: `ESP-IDF: Flash your project`
- Monitor: `ESP-IDF: Monitor your device`
- All-in-one: `ESP-IDF: Build, Flash and Monitor`
- ESP-IDF shell: `ESP-IDF: Create ESP-IDF Terminal`

## Secrets

Create local secrets before running firmware:

1. Copy `main/app_secrets.example.h` to `main/app_secrets.h`
2. Fill in Wi-Fi and MQTT values

Example:

```c
#ifndef APP_SECRETS_H
#define APP_SECRETS_H

#define APP_WIFI_SSID "your-ssid"
#define APP_WIFI_PASS "your-password"
#define APP_MQTT_BROKER_URI "mqtts://your-broker-host:8883"

#endif
```

## Output files

Successful build output:

- App binary: `build/test1.bin`
- Bootloader: `build/bootloader/bootloader.bin`
- ELF: `build/test1.elf`

## Clean build

```powershell
idf.py fullclean
idf.py build
```
