### 컴포넌트 명칭

Jenkins Pipeline은 이 저장소의 루트 [Jenkinsfile](/c:/Users/2-12/Desktop/worktree/VEDA_Final_Project/Jenkinsfile#L1)에 정의된 저장소 전체 CI/CD 오케스트레이션 문서입니다. 현재 파이프라인은 Raspberry Pi K3s 클러스터에 배포되는 서비스 이미지 빌드, Kubernetes 배포, 인증서 자산 처리, DB 마이그레이션, Git Tag 발행, Slack 알림까지 한 번에 담당합니다.

**주요 환경 및 버전**
- 파이프라인 정의 파일: `Jenkinsfile`
- 기본 에이전트: `agent any`
- 배포 대상 플랫폼: `linux/arm64`
- 버전 규칙:
  - Docker Tag: `<MAJOR>.<MINOR>.<BUILD_NUMBER>-<gitShortHash>`
  - Git Tag: `v<MAJOR>.<MINOR>.<BUILD_NUMBER>`

##### 1. 컴포넌트 구성 요소 및 역할 (Component Overview & Module Map)

###### 아키텍처 유형 (Architectural Pattern)

- **적용 유형:** Monorepo Selective CI/CD Pipeline
- **설명:** 이 파이프라인은 저장소 전체를 단일 Jenkins Job으로 관리하되, `changeset` 기반 조건과 수동 실행(`UserIdCause`)을 조합해 필요한 서비스만 선택적으로 배포하는 구조입니다. 이미지 빌드와 K3s 배포, 인증서 Secret 생성, DB 마이그레이션, Git Tag 발행이 하나의 흐름 안에 묶여 있습니다.

###### 주요 구성 (Major Components)

| 구성 요소 | 역할 | 위치 |
| --- | --- | --- |
| Init / Version Stage | 버전 계산, Slack 시작 알림, `develop` 체크아웃 | `Jenkinsfile` |
| Cert Prep Stage | 인증서 번들 해제, 보관, stash 생성 | `Jenkinsfile` |
| Service Deploy Stages | MariaDB, Crow Server, MQTT, MediaMTX, Nginx, Thermal Gateway 배포 | `Jenkinsfile` |
| DB Migration Stage | 2FA 마이그레이션 SQL ConfigMap/Job 실행 | `Jenkinsfile` |
| CCTV Relay Stage | CCTV relay manifest 적용과 관련 배포 재시작 | `Jenkinsfile` |
| Post Actions | Git Tag 발행, Slack 성공/실패 알림 | `Jenkinsfile` |

###### 모듈 상세 (Module Detail)

| Stage | 상세 책임 |
| --- | --- |
| `초기화 및 버전 설정` | `PROJECT_NAME`, `MAJOR_VER`, `MINOR_VER`를 읽어 `DOCKER_VER`, `GIT_TAG_VER`를 계산하고 `develop` 브랜치를 체크아웃합니다. |
| `인증서 준비` | Jenkins credential 번들을 해제해 `RaspberryPi/k3s-cluster/security/certs/` 아래로 복사하고 `certs-stash`로 저장합니다. |
| `CCTV Relay Deploy` | `cctv-relay.yaml`과 CCTV IP ConfigMap을 적용하고 `cctv-relay`, `crow-server`를 재시작합니다. |
| `MariaDB 배포` | MariaDB 이미지를 빌드/푸시하고 DB manifest를 적용합니다. |
| `DB 마이그레이션` | `2fa_migration.sql`을 ConfigMap으로 만들고 `crow-db-migration` Job을 실행합니다. |
| `Crow Server 배포` | Crow Server 이미지를 빌드/푸시하고 Deployment를 재시작합니다. |
| `MQTT 배포` | Mosquitto 이미지를 빌드/푸시하고 `mqtt-certs` Secret을 갱신합니다. |
| `MediaMTX 배포` | 카메라 IP/계정과 읽기 계정을 반영해 렌더링된 manifest를 배포합니다. |
| `Nginx Gateway 배포` | `nginx-certs`, `mtls-ca` Secret을 갱신하고 Nginx 이미지를 빌드/배포합니다. |
| `Thermal DTLS Gateway Deploy` | Thermal Gateway 이미지를 빌드/푸시하고 DTLS Secret 및 NetworkPolicy를 적용합니다. |

##### 2. 시스템 아키텍처 및 특징 (Architecture & Features)

###### Architecture Diagram

```text
GitHub Repository (develop)
          |
          v
     [ Jenkins Job ]
          |
          +--> version calc / Slack start
          +--> cert bundle extract / stash
          +--> docker buildx --platform linux/arm64
          |       |
          |       \--> Docker Hub (service image tags)
          |
          +--> kubectl apply / rollout restart
          |       |
          |       \--> K3s Cluster (MariaDB, Crow, MQTT, MediaMTX, Nginx, Thermal)
          |
          +--> DB migration job
          +--> Git tag push
          \--> Slack success/failure
```

###### Features

- **기능 1:** 변경 경로 기반으로 필요한 서비스만 선택적으로 빌드/배포합니다.
- **기능 2:** 수동 실행 시 대부분의 stage가 `triggeredBy 'UserIdCause'` 조건으로 함께 실행됩니다.
- **기능 3:** Docker Hub에 버전 태그와 `latest`를 동시에 푸시합니다.
- **기능 4:** Kubernetes Secret 생성과 manifest 적용을 파이프라인 안에서 처리합니다.
- **기능 5:** 성공 시 Git Tag를 발행하고 실패/성공 모두 Slack으로 통지합니다.
- **기능 6:** 인증서 번들을 stash와 artifact로 남겨 후속 stage와 운영 검증에 재사용합니다.

##### 3. 개발 환경 구축 및 의존성 (Requirements & Dependencies)

###### Requirements

- **Jenkins 에이전트:** `sh`, `git`, `docker`, `docker buildx`, `kubectl` 사용 가능해야 함
- **접속 권한:** Docker Hub push 권한, GitHub push 권한, K3s kubeconfig 접근 권한
- **도구:** `unzip`, `jar`, `tar` 중 하나 이상, `sed`, Bash
- **플러그인/기능:** Slack 알림, Jenkins Credentials, Artifact Archive

###### 경로 및 설정 (Path Configurations)

- **핵심 파일**
  - [Jenkinsfile](/c:/Users/2-12/Desktop/worktree/VEDA_Final_Project/Jenkinsfile#L1)
- **서비스 배포 경로**
  - `RaspberryPi/k3s-cluster/mariadb/`
  - `RaspberryPi/k3s-cluster/crow_server/`
  - `RaspberryPi/k3s-cluster/mosquitto/`
  - `RaspberryPi/k3s-cluster/mediamtx/`
  - `RaspberryPi/k3s-cluster/nginx/`
  - `RaspberryPi/k3s-cluster/thermal_dtls_gateway/`
- **인증서 작업 경로**
  - `RaspberryPi/k3s-cluster/security/certs/`
- **임시 렌더링 파일**
  - `/tmp/mediamtx.rendered.yaml`

###### Dependency Setup

Jenkins에 최소한 다음 credential이 준비되어 있어야 파이프라인이 정상 동작합니다.

| Credential ID | 타입 | 용도 |
| --- | --- | --- |
| `docker-hub-login` | Username/Password | Docker Hub 로그인 |
| `k3s-kubeconfig` | Secret File | `kubectl` 배포 |
| `github-login` | Username/Password | Git Tag push |
| `thermal-dtls-secret-yaml` | Secret File | Thermal DTLS Secret 적용 |
| `all-certs-bundle` | Secret File | 인증서 번들 해제 |
| `cctv-camera-ip` | Secret Text | MediaMTX 카메라 IP 치환 |
| `cctv-camera-user` | Secret Text | MediaMTX 카메라 사용자명 치환 |
| `cctv-camera-pw` | Secret Text | MediaMTX 카메라 비밀번호 치환 |
| `mediamtx-rtsp-read` | Username/Password | MediaMTX 읽기 계정 치환 |
| `qt-client-env` | Secret File | 비활성화된 Qt stage용 |

##### 4. 설정 가이드 (Configuration)

###### 설정 파일명 1: `Jenkinsfile`

- 전역 environment
  - `GIT_URL`
  - `DOCKER_CRED`
  - `KUBE_CONFIG`
  - `GIT_CREDENTIAL_ID`
  - `THERMAL_DTLS_SECRET_CRED`
- 기본 checkout 브랜치: `develop`

###### 파라미터 설정

- `PROJECT_NAME`
- `MAJOR_VER`
- `MINOR_VER`

성공 후 Job property로 아래 값이 다시 고정됩니다.

- `disableConcurrentBuilds()`
- 위 3개 string parameter

###### 버전 규칙

- `DOCKER_VER = <major>.<minor>.<BUILD_NUMBER>-<gitShortHash>`
- `GIT_TAG_VER = v<major>.<minor>.<BUILD_NUMBER>`
- `currentBuild.displayName = <PROJECT_NAME>_v<major>.<minor>.<BUILD_NUMBER>_<yyyyMMdd>_R<BUILD_NUMBER>`

###### 중요 운영 규칙

- 파이프라인은 시작 시 항상 `develop` 브랜치를 checkout합니다.
- 수동 실행(`UserIdCause`)은 거의 모든 배포 stage를 통과시킬 수 있으므로 전체 배포 성격으로 취급해야 합니다.
- `MediaMTX`, `MQTT`, `Nginx`는 `rollout restart`까지만 수행하고, 일부 stage만 `rollout status`를 기다립니다.
- Qt Windows 배포 stage는 현재 주석 처리되어 비활성화 상태입니다.

##### 5. 빌드 및 정적 분석 (Build & Static Analysis)

###### Build Process

현재 Jenkins가 빌드하는 이미지 목록은 다음과 같습니다.

| 서비스 | 이미지 태그 |
| --- | --- |
| MariaDB | `hjuohj/mariadb-server:${DOCKER_VER}`, `latest` |
| Crow Server | `hjuohj/crow-server:${DOCKER_VER}`, `latest` |
| MQTT | `hjuohj/mqtt-broker:${DOCKER_VER}`, `latest` |
| MediaMTX | `hjuohj/mediamtx-server:${DOCKER_VER}`, `latest` |
| Nginx Gateway | `hjuohj/nginx-gateway:${DOCKER_VER}`, `latest` |
| Thermal DTLS Gateway | `hjuohj/thermal-dtls-gateway:${DOCKER_VER}`, `latest` |

빌드 명령의 공통 패턴은 다음과 같습니다.

```bash
docker buildx build --platform linux/arm64 \
  -t <image>:${DOCKER_VER} \
  -t <image>:latest \
  --push .
```

###### Static Analysis

현재 Jenkinsfile에는 별도의 정적 분석 stage는 없습니다. 즉, 아래 항목은 파이프라인 바깥에서 보완하는 것이 좋습니다.

- `kubectl apply --dry-run=client`
- Dockerfile lint
- YAML lint
- C++/Qt 정적 분석

##### 6. 테스트 및 실행 (Test & Run)

###### Run (실행 방법)

Jenkins Job 실행 방식은 두 가지입니다.

1. `develop` 기준 변경 감지 후 자동 실행
2. Jenkins UI에서 파라미터를 넣어 수동 실행

권장 수동 실행 예시:

- `PROJECT_NAME=AEGIS`
- `MAJOR_VER=1`
- `MINOR_VER=0`

###### Test (검증 방법)

- 콘솔 로그에서 `DOCKER_VER`, `GIT_TAG_VER` 생성 확인
- 필요한 stage만 실행되었는지 `when` 조건 결과 확인
- Docker Hub에 버전 태그와 `latest`가 생성되었는지 확인
- `kubectl rollout status`가 있는 stage는 완료 여부를 확인
- Slack 시작/성공/실패 메시지가 정상 전송되었는지 확인

##### 7. 통신 프로토콜 및 제어 로직 (Control Lifecycle & Protocol)

###### Control Lifecycle

- **Start:** 초기 stage가 버전을 계산하고 Slack 시작 알림을 보낸 뒤 `develop` 브랜치를 checkout합니다.
- **Conditional Deploy:** 각 서비스 stage는 `changeset` 또는 수동 실행 조건에 따라 선택적으로 동작합니다.
- **Secret / Render:** 인증서 번들을 해제하고, MediaMTX는 실제 카메라 자격증명으로 manifest를 렌더링합니다.
- **Deploy:** Docker 이미지 push 후 Kubernetes manifest를 적용하고 필요시 rollout 재시작 및 완료 대기를 수행합니다.
- **Post Success:** Git Tag를 생성해 원격 저장소로 push하고 Slack 성공 메시지를 발송합니다.
- **Post Failure:** Slack 실패 메시지를 발송합니다.

###### Command Reference

| Stage | 실행 조건 |
| --- | --- |
| `초기화 및 버전 설정` | 항상 실행 |
| `인증서 준비` | `security/**`, `nginx/**`, `mosquitto/**`, `Qt_Client/**`, 수동 실행 |
| `CCTV Relay Deploy` | `crow_server/cctv-relay.yaml`, 수동 실행 |
| `MariaDB 배포` | `mariadb/**`, 수동 실행 |
| `DB 마이그레이션` | `mariadb/**`, `crow-db-migration-job.yaml`, `2fa_migration.sql`, 수동 실행 |
| `Crow Server 배포` | `crow_server/CMakeLists.txt`, `Dockerfile`, `crow-server.yaml`, `include/**`, `src/**`, `swagger/**`, 수동 실행 |
| `MQTT 배포` | `mosquitto/**`, `security/**`, 수동 실행 |
| `MediaMTX 배포` | `mediamtx/**`, 수동 실행 |
| `Nginx Gateway 배포` | `nginx/**`, `security/**`, 수동 실행 |
| `Thermal DTLS Gateway Deploy` | `thermal_dtls_gateway/**`, 수동 실행 |

###### Stream/Data Format

| 구분 | 형식 | 예시 |
| --- | --- | --- |
| Job Input | Jenkins String Parameter | `PROJECT_NAME`, `MAJOR_VER`, `MINOR_VER` |
| Secret Input | Jenkins Credential | kubeconfig, Docker Hub, cert bundle |
| Rendered Output | Kubernetes YAML | `/tmp/mediamtx.rendered.yaml` |
| Artifact Output | 파일 보관 | `security/certs/**`, client cert, `rootCA.crt` |
| Release Output | Git Tag / Docker Tag | `v1.0.52`, `1.0.52-a1b2c3d` |

##### 8. 문제 해결 및 운영 체크리스트 (Troubleshooting & Checklist)

###### Troubleshooting

| 증상 | 원인 | 해결책 |
| --- | --- | --- |
| 예상한 stage가 실행되지 않음 | `changeset` 경로가 조건과 맞지 않음 | 변경 파일 경로와 `when` 조건을 대조하거나 수동 실행 사용 |
| 수동 실행 시 너무 많은 stage가 실행됨 | `triggeredBy 'UserIdCause'`가 대부분 stage에 포함됨 | 수동 실행은 전체 배포 성격으로 인지하고 사용 |
| 인증서 준비 stage 실패 | 번들 형식 미지원 또는 `rootCA.crt`, `server.crt`, `server.key` 누락 | `all-certs-bundle` 내용과 압축 형식 확인 |
| Docker push 실패 | `docker-hub-login` 문제 또는 buildx 환경 문제 | Jenkins credential과 `docker buildx` 사용 가능 여부 확인 |
| Kubernetes 배포 실패 | `k3s-kubeconfig` 문제 또는 manifest 오류 | kubeconfig, namespace, `kubectl apply` 로그 확인 |
| Git Tag push 실패 | 이미 존재하는 태그 또는 GitHub 자격증명 문제 | 동일 태그 존재 여부와 `github-login` 확인 |
| MediaMTX 배포 실패 | 카메라 credential 치환 실패 | `cctv-camera-*`, `mediamtx-rtsp-read` credential 확인 |

###### Operational Checklist

- `docker-hub-login`, `k3s-kubeconfig`, `github-login`, `all-certs-bundle`, `thermal-dtls-secret-yaml`이 준비되었는가
- 수동 실행 시 전체 배포에 가까운 동작을 한다는 점을 알고 있는가
- `develop` 브랜치 기준으로 배포된다는 점을 확인했는가
- Docker Hub 이미지 태그와 Git Tag 규칙이 릴리즈 정책과 맞는가
- `rollout status`를 기다리지 않는 stage는 배포 후 수동 검증을 했는가

**작성자:** VEDA Team  
**마지막 업데이트:** 2026-03-19
