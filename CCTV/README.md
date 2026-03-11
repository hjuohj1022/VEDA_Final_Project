# CCTV Depth Server (TensorRT + CUDA)

RTSP CCTV 영상을 입력으로 받아 TensorRT 엔진 기반 깊이 추론을 수행하고, TCP 제어/스트리밍 인터페이스를 제공하는 Windows 전용 서버입니다.

## Overview
| Component | Role | Main File |
|---|---|---|
| Inference Server | RTSP 수신, CUDA 전처리, TensorRT 추론, TCP 제어 | `runtime/main.cpp` |
| Desktop Client | 서버 제어 + 스트림 시각화 | `tools/client/client_gui.py` |
| Model Export | DA3 safetensors -> ONNX | `tools/export/export_da3metric_onnx.py` |
| Bootstrap Script | 의존성 다운로드 + 경로 설정 생성 | `tools/bootstrap/setup_dependencies.ps1` |

## Runtime Module Map
핵심 런타임 모듈은 아래처럼 책임을 분리했습니다.

| Module | Responsibility |
|---|---|
| `runtime/main.cpp` | 엔트리포인트, accept loop, 요청 파싱/디스패치 호출 |
| `runtime/server_runtime.*` | Winsock 초기화/바인드/리스닝/정리 |
| `runtime/command_dispatcher.*` | 요청 분기 처리, worker/stream 스레드 제어 |
| `runtime/depth_worker.cu` | CUDA 전처리 커널 + TensorRT 추론 루프 |
| `runtime/request.*` | 요청 문자열 파싱 |
| `runtime/request_validator.*` | 요청 유효성 검사 (채널/스트림 상호배타 등) |
| `runtime/net_protocol.*` | TCP 응답/스트림 바이너리 송신 |
| `runtime/trt_engine.*` | TensorRT 엔진/버퍼 초기화 |
| `runtime/runtime_config.*` | 런타임 튜닝 파라미터 |

## Architecture Diagram
```text
                           +----------------------------------+
                           |     tools/client/client_gui.py   |
                           |  - Start/Stop/Pause/Resume       |
                           |  - View PC (Server/Client)       |
                           +----------------+-----------------+
                                            | TCP commands
                                            | (channel, mode, pc_view, ...)
                                            v
+-------------------+            +----------+-----------+            +------------------+
| RTSP Cameras      | --RTSP-->  |    depth_trt.exe    | --depth--> | depth_stream     |
| ch0..ch3          |            | (runtime/main.cpp, TCP 9090) | --rgbd---> | rgbd_stream      |
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
                            | tools/export/export_da3metric_onnx.py |
                            | + DA3 checkpoint              |
                            +--------------------------------+
```

## Quick Start
```powershell
# 1) 의존성 자동 설치 (OpenCV/TensorRT/DA3Metric + config/local_paths.cmake)
powershell -ExecutionPolicy Bypass -File .\setup_dependencies.ps1

# 2) 환경 설정 (필수)
# - config/app_config.h: ENGINE_PATH, RTSP_URLS, INPUT_HEIGHT/WIDTH 점검

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
  runtime/
    main.cpp
    server_runtime.h/.cpp
    command_dispatcher.h/.cpp
    depth_worker.cu
    request.h/.cpp
    request_validator.h/.cpp
    net_protocol.h/.cpp
    trt_engine.h/.cpp
    runtime_config.h/.cpp
    request_parser_smoke.cpp
    runner.h pointcloud.h ...
  config/
    app_config.h(.example)
    local_paths.cmake(.example)
  tools/
    bootstrap/setup_dependencies.ps1
    client/client_gui.py
    client/mtls_external_test.sh
    client/view_ply.py
    export/export_da3metric_onnx.py
  ml_assets/
    checkpoints/
    onnx/
    engines/
    sources/
  CMakeLists.txt
  setup_dependencies.ps1      # wrapper
  client_gui.py               # wrapper
  view_ply.py                 # wrapper
```

## Requirements
- Windows 10/11 x64
- NVIDIA GPU (CUDA 지원)
- NVIDIA Driver (최신 권장)
- CUDA Toolkit 12.x
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
- DA3Metric 체크포인트 다운로드 (`ml_assets/checkpoints/DA3METRIC-LARGE`)
- `config/local_paths.cmake` 자동 생성

자주 쓰는 옵션:
```powershell
.\setup_dependencies.ps1 `
  -CudaPath "<CUDA_PATH>" `
  -TensorRtUrl "<direct-zip-url>" `
  -SkipOpenCV -SkipTensorRT -SkipDa3Metric `
  -Force
```

## Configuration
### `app_config.h`
`app_config.h.example`를 기준으로 다음 항목을 설정합니다.

- `ENGINE_PATH`: TensorRT 엔진 경로(프로젝트 루트 기준 상대경로 권장)
- `RTSP_URLS`: 채널별 RTSP 주소
- `RTSP_CHANNEL`: 기본 채널
- `INPUT_HEIGHT`, `INPUT_WIDTH`: 엔진 입력 shape와 반드시 일치

### RTSPS(mTLS)
- 기본 URL 예시는 `rtsps://...` 스킴을 사용합니다.
- 런타임 기본 FFmpeg 캡처 옵션에 mTLS 설정이 포함됩니다.
- 이 구간은 카메라 입력용 인증서로 `certs/RTSP/cctv.crt`, `certs/RTSP/cctv.key`를 계속 사용합니다.
  - `tls_verify;1`
  - `ca_file;certs/RTSP/rootCA.crt`
  - `cert_file;certs/RTSP/cctv.crt`
  - `key_file;certs/RTSP/cctv.key`

### `config/local_paths.cmake`
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

빌드 후 자동 복사:
- `build/Release/ml_assets/engines/`로 `ml_assets/engines`가 자동 복사됩니다.
- 따라서 `ENGINE_PATH=ml_assets/engines/...` 설정이면 `build/Release` 단독 실행 시에도 엔진 경로가 유지됩니다.
- `build/Release/certs/`로 `certs` 디렉터리 전체가 자동 복사됩니다(mTLS/RTSPS 인증서 배포용).

## Test
```powershell
# 스모크 테스트 빌드 + 실행
cmake --build build --config Release --target request_parser_smoke
.\build\Release\request_parser_smoke.exe

# ctest 통합 실행
ctest --test-dir build -C Release --output-on-failure
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

보안 연결(mTLS):
- `client_gui.py`는 `mTLS` 체크 시 클라이언트 인증서로 TLS 소켓을 직접 연결합니다.
- 기본 포트는 `9090`이며, 아래 파일 경로를 사용합니다.
  - `certs/forTestClient/rootCA.crt`
  - `certs/forTestClient/cctv.crt`
  - `certs/forTestClient/cctv.key`

서버 제어채널 mTLS:
- `depth_trt`는 OpenSSL 기반 mTLS 핸드셰이크를 직접 처리합니다(별도 stunnel 불필요).
- 이 구간은 RTSPS 입력과 별개로 `certs/mTLS/server.crt`, `certs/mTLS/server.key`를 사용합니다.
- 런타임 설정(`runtime/runtime_config.h`) 기본값:
  - `control_tls.enabled=true`
  - `control_tls.require_client_cert=true`
  - `control_tls.ca_file=certs/mTLS/rootCA.crt`
  - `control_tls.cert_file=certs/mTLS/server.crt`
  - `control_tls.key_file=certs/mTLS/server.key`
  - `control_tls.ssl_dll=libssl-1_1-x64.dll`
  - `control_tls.crypto_dll=libcrypto-1_1-x64.dll`

외부(Linux) mTLS 점검:
- 스크립트: `tools/client/mtls_external_test.sh`
- 목적:
  - 클라이언트 인증서 없음 -> 거부 확인
  - 클라이언트 인증서 포함 -> `OK ...` 응답 확인
- 예시:
```bash
chmod +x ./tools/client/mtls_external_test.sh
./tools/client/mtls_external_test.sh --host <SERVER_TAILNET_IP> --ca certs/forTestClient/rootCA.crt --cert certs/forTestClient/cctv.crt --key certs/forTestClient/cctv.key --port 9090 --timeout 8
```

운영 토폴로지별 테스트 경로:

| Topology | 동작 방식 | 권장 테스트 대상 |
|---|---|---|
| Tailnet 노드 -> 서버(직접) | 클라이언트 노드가 Tailscale 설치 + kernel TUN | `100.x.x.x:9090` |
| Tailnet 마스터(advertise) -> 워커 서브넷(인입) | Tailnet 클라이언트가 advertise된 서브넷으로 접근 | 워커 LAN IP:`9090` |
| 워커(비-Tailscale) -> Tailnet 서버(역방향 필요) | 기본 advertise만으로는 미지원, 라우트/NAT 또는 터널 필요 | `ssh -L` 사용 시 `127.0.0.1:19090` |

참고:
- `advertise-routes`는 기본적으로 `Tailnet -> 서브넷` 경로입니다.
- 워커가 직접 `100.x`로 나가려면 추가 라우팅/NAT 또는 터널 구성이 필요합니다.

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
python .\tools\export\export_da3metric_onnx.py --height 560 --width 1008
```

3. 엔진 생성:
```powershell
.\<TENSORRT_ROOT>\bin\trtexec.exe `
  --onnx=ml_assets/onnx/da3metric_560x1008.onnx `
  --saveEngine=ml_assets/engines/da3metric_560x1008_fp16.engine `
  --fp16 --verbose
```

4. `app_config.h`의 `ENGINE_PATH` 업데이트

## Troubleshooting
- 엔진 로드 실패: `ENGINE_PATH` 존재 여부 + 권한 + 경로 오타 확인
- 추론 shape 오류: `INPUT_HEIGHT/WIDTH`와 엔진 입력 shape 불일치 여부 확인
- RTSP 접속 실패: URL/계정/포트/네트워크/카메라 채널 점검
- TensorRT 다운로드 실패: `-TensorRtUrl`로 직접 ZIP URL 지정
- DLL 관련 실행 오류: 빌드 산출물 폴더에 TensorRT/OpenCV DLL 복사 여부 확인
- 외부 mTLS 테스트에서 `tailscale ping`은 성공하지만 `openssl/curl/nc` TCP가 타임아웃이면:
  - 소스 노드 `tailscaled` 실행 인자를 확인합니다.
  - `--tun=userspace-networking`이면 일반 TCP 테스트가 실패할 수 있으므로 kernel TUN 모드(`--tun=tailscale0`)로 전환 후 재시도합니다.
  - 확인 명령:
    - `cat /proc/$(pidof tailscaled)/cmdline | tr '\0' ' '; echo`
- 워커 노드에 `ip/route` 유틸이 없어 `100.x` 라우트 설정이 불가하면:
  - `ssh -L` 포트포워딩으로 우회 테스트합니다.
  - 예: `ssh -f -N -L 19090:<SERVER_TAILNET_IP>:9090 <master_user>@<MASTER_LAN_IP>`
  - 이후 워커에서 `127.0.0.1:19090` 대상으로 `mtls_external_test.sh` 실행

## Operational Checklist
- `config/local_paths.cmake` 경로가 실제 설치 경로와 일치하는가?
- `app_config.h`의 엔진/RTSP/입력 해상도가 현재 배포 아티팩트와 일치하는가?
- 서버 로그에 `Listening on port ...`가 출력되는가?
- 클라이언트 `Start` 응답이 `OK started ...`로 오는가?
