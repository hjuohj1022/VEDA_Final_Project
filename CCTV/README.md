# CCTV Depth Server (TensorRT + CUDA)

RTSP CCTV 영상을 입력으로 받아 TensorRT 엔진 기반 깊이 추론을 수행하고, TCP 제어/스트리밍 인터페이스를 제공하는 Windows 전용 서버입니다.

## Overview
| Component | Role | Main File |
|---|---|---|
| Inference Server | RTSP 수신, CUDA 전처리, TensorRT 추론, TCP 제어 | `main.cu` |
| Desktop Client | 서버 제어 + 스트림 시각화 | `client_gui.py` |
| Model Export | DA3 safetensors -> ONNX | `fact/export_da3metric_onnx.py` |
| Bootstrap Script | 의존성 다운로드 + 경로 설정 생성 | `setup_dependencies.ps1` |

## Architecture Diagram
```text
                           +----------------------------------+
                           |          client_gui.py           |
                           |  - Start/Stop/Pause/Resume       |
                           |  - View PC (Server/Client)       |
                           +----------------+-----------------+
                                            | TCP commands
                                            | (channel, mode, pc_view, ...)
                                            v
+-------------------+            +----------+-----------+            +------------------+
| RTSP Cameras      | --RTSP-->  |    depth_trt.exe    | --depth--> | depth_stream     |
| ch0..ch3          |            | (main.cu, TCP 9090) | --rgbd---> | rgbd_stream      |
+-------------------+            |  CUDA + TensorRT     | --png----> | pc_stream        |
                                 +----------+-----------+            +------------------+
                                            |
                                            | load engine
                                            v
                            +---------------+----------------+
                            | TensorRT Engine (.engine)      |
                            | from DA3Metric ONNX export     |
                            +---------------+----------------+
                                            ^
                                            | ONNX export
                            +---------------+----------------+
                            | fact/export_da3metric_onnx.py |
                            | + DA3 checkpoint              |
                            +--------------------------------+
```

## Quick Start
```powershell
# 1) 의존성 자동 설치 (OpenCV/TensorRT/DA3Metric + local_paths.cmake)
powershell -ExecutionPolicy Bypass -File .\setup_dependencies.ps1

# 2) 환경 설정 (필수)
# - app_config.h: ENGINE_PATH, RTSP_URLS, INPUT_HEIGHT/WIDTH 점검

# 3) 빌드
cmake -S . -B build
cmake --build build --config Release

# 4) 서버 실행
.\build\Release\depth_trt.exe --port=9090

# 5) 클라이언트 실행 (선택)
python .\client_gui.py
```

## Features
- RTSP 채널 선택 실행 (`channel=0~3`)
- 실행 모드 전환 (`headless`, `gui`)
- 런타임 제어 (`pause`, `resume`, `stop`)
- 포인트클라우드 시점 제어 (`pc_view rx/ry/flipx/flipy/flipz/wire/mesh`)
- 스트리밍 인터페이스
  - `depth_stream`: depth32f
  - `rgbd_stream`: depth32f + bgr24
  - `pc_stream`: png
- DA3 Metric 체크포인트 자동 다운로드 + ONNX export 파이프라인

## Directory Layout
```text
CCTV/
  main.cu
  app_config.h
  app_config.h.example
  CMakeLists.txt
  setup_dependencies.ps1
  client_gui.py
  local_paths.cmake.example
  fact/
    export_da3metric_onnx.py
```

## Requirements
- Windows 10/11 x64
- NVIDIA GPU (CUDA 지원)
- NVIDIA Driver (최신 권장)
- CUDA Toolkit 12.1 (기본 경로 기준)
- Visual Studio 2022 (MSVC v143, x64)
- CMake 3.10+
- Python 3.10+

## Dependency Bootstrap
기본 실행:
```powershell
.\setup_dependencies.ps1
```

기본 동작:
- OpenCV 4.10.0 다운로드/압축해제
- TensorRT 10.15.1.29 다운로드/압축해제
  - `-TensorRtUrl` 미지정 시 `-CudaPath`의 CUDA 버전 태그로 URL 자동 선택
  - 예: CUDA `v12.1` -> `cuda-12.1` 우선, 실패 시 `cuda-12.9` fallback
- DA3Metric 체크포인트 다운로드 (`fact/checkpoints/DA3METRIC-LARGE`)
- `local_paths.cmake` 자동 생성

자주 쓰는 옵션:
```powershell
.\setup_dependencies.ps1 `
  -CudaPath "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.1" `
  -TensorRtUrl "<direct-zip-url>" `
  -SkipOpenCV -SkipTensorRT -SkipDa3Metric `
  -Force
```

## Configuration
### `app_config.h`
`app_config.h.example`를 기준으로 다음 항목을 설정합니다.

- `ENGINE_PATH`: TensorRT 엔진 절대 경로
- `RTSP_URLS`: 채널별 RTSP 주소
- `RTSP_CHANNEL`: 기본 채널
- `INPUT_HEIGHT`, `INPUT_WIDTH`: 엔진 입력 shape와 반드시 일치

### `local_paths.cmake`
자동 생성 파일이며, 수동 편집 시 아래 경로를 정확히 지정해야 합니다.

- `TRT_PATH`
- `OPENCV_PATH`
- `CUDA_PATH`

## Build
```powershell
cmake -S . -B build
cmake --build build --config Release
```

Output:
```text
build/Release/depth_trt.exe
```

## Run
서버:
```powershell
.\build\Release\depth_trt.exe --port=9090
```

클라이언트:
```powershell
python .\client_gui.py
```

## TCP Command Reference
기본 명령 예시:
```text
channel=0 headless
channel=1 gui
pause
resume
stop
pc_view rx=-20 ry=35 flipx=0 flipy=0 flipz=0 wire=1 mesh=0
depth_stream
rgbd_stream
pc_stream
```

응답 예시:
- `OK started channel=0 mode=headless`
- `OK pause=1`
- `OK stopped`
- `OK pc_view rx=... ry=... flipx=... flipy=... flipz=... wire=... mesh=...`

## Stream Binary Format
모든 헤더 정수는 little-endian `uint32`입니다.

| Stream | ACK | Header | Payload |
|---|---|---|---|
| `depth_stream` | `OK depth_stream\n` | `[frameIdx,w,h,payloadBytes]` (16 bytes) | `float32 depth[w*h]` |
| `rgbd_stream` | `OK rgbd_stream fmt=depth32f+bgr24\n` | `[frameIdx,w,h,depthBytes,bgrBytes]` (20 bytes) | `float32 depth[w*h]` + `uint8 bgr[w*h*3]` |
| `pc_stream` | `OK pc_stream fmt=png\n` | `[frameIdx,w,h,payloadBytes]` (16 bytes) | `png bytes` |

## DA3Metric -> TensorRT Engine
1. 체크포인트 준비:
```powershell
.\setup_dependencies.ps1 -SkipOpenCV -SkipTensorRT
```

2. ONNX export:
```powershell
python .\fact\export_da3metric_onnx.py --height 560 --width 1008
```

3. 엔진 생성:
```powershell
.\TensorRT-10.15.1.29\bin\trtexec.exe `
  --onnx=fact/da3metric_560x1008.onnx `
  --saveEngine=fact/da3metric_560x1008_fp16.engine `
  --fp16 --verbose
```

4. `app_config.h`의 `ENGINE_PATH` 업데이트

## Troubleshooting
- 엔진 로드 실패: `ENGINE_PATH` 존재 여부 + 권한 + 경로 오타 확인
- 추론 shape 오류: `INPUT_HEIGHT/WIDTH`와 엔진 입력 shape 불일치 여부 확인
- RTSP 접속 실패: URL/계정/포트/네트워크/카메라 채널 점검
- TensorRT 다운로드 실패: `-TensorRtUrl`로 직접 ZIP URL 지정
- DLL 관련 실행 오류: 빌드 산출물 폴더에 TensorRT/OpenCV DLL 복사 여부 확인

## Operational Checklist
- `local_paths.cmake` 경로가 실제 설치 경로와 일치하는가?
- `app_config.h`의 엔진/RTSP/입력 해상도가 현재 배포 아티팩트와 일치하는가?
- 서버 로그에 `Listening on port ...`가 출력되는가?
- 클라이언트 `Start` 응답이 `OK started ...`로 오는가?
