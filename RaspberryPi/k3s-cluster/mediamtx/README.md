### 컴포넌트 명칭

MediaMTX는 CCTV 카메라의 RTSP 스트림을 수집하고, 내부 RTSP와 HLS로 재공급하며, 메인 스트림을 녹화 파일로 저장하는 미디어 릴레이 서비스입니다. 이 디렉터리는 MediaMTX 설정, 카메라 접속 환경변수, 녹화 경로, Kubernetes 배포 manifest를 관리합니다.

**주요 환경 및 버전**
- 베이스 이미지: `bluenviron/mediamtx:latest-ffmpeg`
- 배포 형태: ConfigMap + Deployment + ClusterIP Service
- 주요 포트: `8554/TCP`, `1935/TCP`, `8888/TCP`, `8889/TCP`

##### 1. 컴포넌트 구성 요소 및 역할 (Component Overview & Module Map)

###### 아키텍처 유형 (Architectural Pattern)

- **적용 유형:** Media Ingest + Relay + Recording Service
- **설명:** MediaMTX는 카메라 RTSP 원본을 직접 외부에 노출하지 않고, 내부에서 ffmpeg로 받아 표준화된 경로로 재발행합니다. 외부 클라이언트는 Nginx를 통해 RTSPS/HLS로 접근하고, 녹화 파일은 호스트 경로에 저장됩니다.

###### 주요 구성 (Major Components)

| 구성 요소 | 역할 | 파일 |
| --- | --- | --- |
| MediaMTX Config | ingest 경로, 녹화 정책, 포트 정의 | `mediamtx.yaml`, `mediamtx.yml` |
| Deployment | 컨테이너 실행, ConfigMap/녹화 볼륨 마운트 | `mediamtx.yaml` |
| Service | 내부 RTSP/HLS/WebRTC 노출 | `mediamtx.yaml` |
| Recording Storage | 녹화 파일 저장 경로 | `mediamtx.yaml`, `recording-pvc.yaml` |
| Docker Image | 로컬 테스트용 이미지 정의 | `Dockerfile` |

###### 모듈 상세 (Module Detail)

| 파일 | 상세 책임 |
| --- | --- |
| `mediamtx.yaml` | ConfigMap, Deployment, Service를 한 파일에 정의 |
| `mediamtx.yml` | 로컬/이미지 복사용 MediaMTX 설정 본문 |
| `recording-pvc.yaml` | PVC 기반 녹화 스토리지 예시 |
| `Dockerfile` | 기본 이미지에 `mediamtx.yml` 복사 |

##### 2. 시스템 아키텍처 및 특징 (Architecture & Features)

###### Architecture Diagram

```text
Camera RTSP Endpoint
      |
      | rtsp://CAMERA_USER:CAMERA_PASSWORD@CAMERA_IP/...
      v
[ MediaMTX ]
   |- ffmpeg runOnInit restream
   |- /0/main, /0/sub ... /3/main, /3/sub
   |- main streams recorded to /recordings
   |
   +--> mediamtx-service:8554 (RTSP)
   +--> mediamtx-service:8888 (HLS)
   \--> Nginx :8555 /hls/ outward exposure
```

###### Features

- **기능 1:** 채널 `0`부터 `3`까지 `main`과 `sub` 경로를 각각 분리해 제공합니다.
- **기능 2:** `main` 스트림만 `record: yes`로 녹화해 저장 용량을 제어합니다.
- **기능 3:** `runOnInit` ffmpeg 명령으로 카메라 RTSP를 자동 재연결합니다.
- **기능 4:** HLS는 `8888/TCP`, RTSP는 `8554/TCP`로 내부 서비스하며 외부는 Nginx가 TLS를 종료합니다.

##### 3. 개발 환경 구축 및 의존성 (Requirements & Dependencies)

###### Requirements

- **카메라 연결 정보:** `CAMERA_IP`, `CAMERA_USER`, `CAMERA_PASSWORD`
- **스토리지:** `/home/pi/cctv-recordings` 호스트 경로 쓰기 가능
- **클러스터:** K3s 또는 Kubernetes
- **상위 의존:** Nginx가 `mediamtx-service`를 upstream으로 사용

###### 경로 및 설정 (Path Configurations)

- **핵심 파일**
  - `mediamtx.yaml`
  - `mediamtx.yml`
  - `recording-pvc.yaml`
- **필수 환경변수**
  - `CAMERA_IP`
  - `CAMERA_USER`
  - `CAMERA_PASSWORD`
- **호스트 경로**
  - `/home/pi/cctv-recordings`
- **고정 노드**
  - `pi-worker1`

###### Dependency Setup

`mediamtx.yaml`의 placeholder를 실제 카메라 값으로 바꾼 뒤 배포합니다.

```bash
kubectl apply -f RaspberryPi/k3s-cluster/mediamtx/mediamtx.yaml
```

##### 4. 설정 가이드 (Configuration)

###### 설정 파일명 1: `mediamtx.yaml`

- ConfigMap 이름: `mediamtx-config`
- Deployment 이름: `mediamtx-server`
- Service 이름: `mediamtx-service`
- 노드 고정: `pi-worker1`
- 녹화 볼륨: `hostPath /home/pi/cctv-recordings`

###### 설정 파일명 2: 스트림 경로 정의

- 메인 스트림: `/0/main`, `/1/main`, `/2/main`, `/3/main`
- 서브 스트림: `/0/sub`, `/1/sub`, `/2/sub`, `/3/sub`
- 녹화 파일 경로: `/recordings/%path_%Y-%m-%d_%H-%M-%S`
- 녹화 포맷: `fmp4`

###### 포트 및 노출

- `8554/TCP`: 내부 RTSP
- `1935/TCP`: RTMP
- `8888/TCP`: 내부 HLS
- `8889/TCP`: WebRTC
- 외부 RTSPS는 Nginx `8555/TCP`에서 종단

##### 5. 빌드 및 정적 분석 (Build & Static Analysis)

###### Build Process

로컬 이미지 빌드가 필요하면 다음과 같이 수행할 수 있습니다.

```bash
docker build -t local/mediamtx:dev RaspberryPi/k3s-cluster/mediamtx
```

Kubernetes 적용:

```bash
kubectl apply -f RaspberryPi/k3s-cluster/mediamtx/mediamtx.yaml
```

###### Static Analysis

- `kubectl apply --dry-run=client -f mediamtx.yaml`
- `CAMERA_*` placeholder 치환 여부 확인
- `/home/pi/cctv-recordings` 경로 권한 확인

##### 6. 테스트 및 실행 (Test & Run)

###### Run (실행 방법)

```bash
kubectl get pods -l app=mediamtx -o wide
kubectl get svc mediamtx-service
kubectl logs deploy/mediamtx-server
```

###### Test (검증 방법)

- Pod가 `Running`
- `mediamtx-service`가 생성됨
- 로그에 `runOnInit` ffmpeg 오류가 없는지 확인
- Nginx를 통한 RTSPS/HLS 접근이 되는지 확인

외부 RTSPS 확인 예시:

```bash
openssl s_client -connect <LB_IP>:8555 \
  -cert client-qt.crt -key client-qt.key -CAfile rootCA.crt
```

##### 7. 통신 프로토콜 및 제어 로직 (Control Lifecycle & Protocol)

###### Control Lifecycle

- **Start:** Deployment 기동 후 각 채널별 `runOnInit` ffmpeg 프로세스가 카메라 RTSP를 받아 MediaMTX 내부 경로로 재발행합니다.
- **Serve:** 내부에서는 `mediamtx-service:8554`와 `:8888`로 서비스되고, 외부는 Nginx가 RTSPS/HLS로 중계합니다.
- **Record:** `main` 경로만 녹화되어 `/home/pi/cctv-recordings`에 저장됩니다.

###### Command Reference

| 구분 | 경로/명령 | 설명 |
| --- | --- | --- |
| 내부 RTSP | `rtsp://mediamtx-service:8554/0/main` | 채널 0 메인 스트림 |
| 내부 RTSP | `rtsp://mediamtx-service:8554/0/sub` | 채널 0 서브 스트림 |
| 외부 RTSPS | `rtsps://<LB_IP>:8555/0/main` | Nginx 경유 TLS 스트림 |
| 외부 HLS | `https://<LB_IP>/hls/` | HLS 경로 진입점 |
| 운영 | `kubectl logs deploy/mediamtx-server` | ingest 상태 확인 |

###### Stream/Data Format

| 항목 | 형식 |
| --- | --- |
| 카메라 입력 | RTSP |
| 내부 배포 | RTSP, HLS, RTMP, WebRTC |
| 외부 배포 | RTSPS, HLS |
| 녹화 파일 | `fmp4` |

##### 8. 문제 해결 및 운영 체크리스트 (Troubleshooting & Checklist)

###### Troubleshooting

| 증상 | 원인 | 해결책 |
| --- | --- | --- |
| 스트림이 생성되지 않음 | `CAMERA_*` 값 또는 카메라 RTSP URL 문제 | 로그와 placeholder 치환 여부 확인 |
| 녹화 파일이 생기지 않음 | 녹화 경로 권한 문제 | `/home/pi/cctv-recordings` 쓰기 권한 확인 |
| 외부 RTSPS 실패 | Nginx stream 프록시 문제 | `nginx-service:8555`, 인증서, upstream 확인 |
| HLS 접근 실패 | Nginx `/hls/` 프록시 또는 MediaMTX `8888` 문제 | `mediamtx-service`와 Nginx 설정 대조 |

###### Operational Checklist

- `CAMERA_IP`, `CAMERA_USER`, `CAMERA_PASSWORD`가 실제 값으로 치환되었는가
- `pi-worker1` 노드가 존재하는가
- `/home/pi/cctv-recordings` 경로가 생성되고 쓰기 가능한가
- `kubectl logs deploy/mediamtx-server`에서 ffmpeg 오류가 없는가
- 외부 RTSPS/HLS 경로가 Nginx를 통해 접근 가능한가

**작성자:** A.E.G.I.S Team  
**마지막 업데이트:** 2026-03-19
