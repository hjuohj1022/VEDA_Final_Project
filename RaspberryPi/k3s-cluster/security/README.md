### 컴포넌트 명칭

Security Assets 디렉터리는 Nginx Gateway와 Mosquitto가 사용하는 mTLS 인증서와 관련 Secret 매핑 가이드를 관리합니다. 이 디렉터리의 핵심은 `generate_certs.sh`이며, 루트 CA, 서버 인증서, 클라이언트 인증서를 생성해 운영 배포에 필요한 신뢰 체인을 준비합니다.

**주요 환경 및 버전**
- 도구: `openssl`
- 기본 유효기간: `3650`일
- 기본 SAN 대상: `veda.team3.com`, `192.168.55.200`

##### 1. 컴포넌트 구성 요소 및 역할 (Component Overview & Module Map)

###### 아키텍처 유형 (Architectural Pattern)

- **적용 유형:** Certificate Asset Generation and Secret Mapping
- **설명:** 이 디렉터리는 실행 서비스가 아니라 인증 자산을 준비하는 보안 지원 레이어입니다. Nginx HTTPS/MQTTS/RTSPS 서버 인증서와 테스트/장치용 클라이언트 인증서를 만들어 Kubernetes Secret에 주입할 수 있도록 돕습니다.

###### 주요 구성 (Major Components)

| 구성 요소 | 역할 | 파일 |
| --- | --- | --- |
| Root CA Generator | 루트 인증서와 키 생성 | `generate_certs.sh` |
| Server Cert Generator | Nginx/MQTT/RTSPS용 서버 인증서 생성 | `generate_certs.sh` |
| Client Cert Generator | Qt, CCTV, STM32 클라이언트 인증서 생성 | `generate_certs.sh` |
| Generated Assets | 생성된 PEM/KEY/CSR 보관 | `certs/nginx-mtls/`, `cctv-certs/` |

###### 모듈 상세 (Module Detail)

| 파일/디렉터리 | 상세 책임 |
| --- | --- |
| `generate_certs.sh` | Root CA, 서버/클라이언트 인증서 생성과 검증 수행 |
| `certs/nginx-mtls/` | 기본 생성 출력 경로 |
| `cctv-certs/` | 기존 CCTV 제어 인증서 자산 보관 |

##### 2. 시스템 아키텍처 및 특징 (Architecture & Features)

###### Architecture Diagram

```text
[ generate_certs.sh ]
        |
        +--> rootCA.crt / rootCA.key
        +--> server.crt / server.key
        +--> client-qt.crt / client-qt.key
        +--> client-cctv.crt / client-cctv.key
        \--> client-stm32.crt / client-stm32.key

Generated assets
  - nginx-certs Secret
  - mtls-ca Secret
  - mqtt-certs Secret
```

###### Features

- **기능 1:** 루트 CA를 자체 발급해 내부 mTLS 체인을 구성합니다.
- **기능 2:** SAN이 포함된 서버 인증서를 생성해 `HTTPS/MQTTS/RTSPS`에서 재사용합니다.
- **기능 3:** Qt, CCTV, STM32용 클라이언트 인증서를 별도로 발급합니다.
- **기능 4:** 생성 직후 `openssl verify`로 기본 체인 검증을 수행합니다.

##### 3. 개발 환경 구축 및 의존성 (Requirements & Dependencies)

###### Requirements

- **도구:** `openssl`, Bash 실행 환경
- **사용 환경:** Linux, macOS, Git Bash/MSYS2 등 Bash 가능 환경
- **연계 서비스:** Nginx Gateway, Mosquitto, Crow Server

###### 경로 및 설정 (Path Configurations)

- **핵심 파일**
  - `generate_certs.sh`
- **기본 환경변수**
  - `CERT_DIR=./certs`
  - `DAYS=3650`
  - `GATEWAY_SERVER_DNS=veda.team3.com`
  - `GATEWAY_SERVER_IP=192.168.55.200`
- **출력 경로**
  - `./certs/nginx-mtls`

###### Dependency Setup

스크립트에 실행 권한을 부여한 뒤 실행합니다.

```bash
cd RaspberryPi/k3s-cluster/security
chmod +x generate_certs.sh
./generate_certs.sh
```

##### 4. 설정 가이드 (Configuration)

###### 설정 파일명 1: `generate_certs.sh`

- 루트 CA 생성: `issue_root_ca`
- 서버 인증서 생성: `issue_server_cert`
- 클라이언트 인증서 생성: `issue_client_cert`
- 기본 출력 경로: `./certs/nginx-mtls`

###### 환경변수 설정

- `CERT_DIR`: 인증서 출력 루트
- `DAYS`: 유효기간
- `GATEWAY_SERVER_DNS`: 서버 인증서 DNS SAN
- `GATEWAY_SERVER_IP`: 서버 인증서 IP SAN

###### Secret 매핑 기준

- `nginx-certs <- server.crt + server.key`
- `mtls-ca <- rootCA.crt`
- `mqtt-certs <- ca.crt(rootCA.crt 복사본) + server.crt + server.key`
- `crow-certs <- 기존 cctv-control 인증서 세트 별도 관리`

##### 5. 빌드 및 정적 분석 (Build & Static Analysis)

###### Build Process

이 디렉터리는 컴파일 대상이 없고, 인증서 생성 스크립트를 실행합니다.

```bash
./generate_certs.sh
```

###### Static Analysis

- 스크립트 내 `openssl verify` 결과 확인
- SAN 값이 실제 게이트웨이 DNS/IP와 일치하는지 확인
- Secret에 넣을 파일명이 서비스 기대값과 맞는지 확인

##### 6. 테스트 및 실행 (Test & Run)

###### Run (실행 방법)

기본 실행:

```bash
./generate_certs.sh
```

환경변수 override 예시:

```bash
CERT_DIR=./certs \
GATEWAY_SERVER_DNS=my-gateway.local \
GATEWAY_SERVER_IP=192.168.55.210 \
./generate_certs.sh
```

###### Test (검증 방법)

```bash
openssl verify -CAfile ./certs/nginx-mtls/rootCA.crt ./certs/nginx-mtls/server.crt
openssl verify -CAfile ./certs/nginx-mtls/rootCA.crt ./certs/nginx-mtls/client-qt.crt
openssl x509 -in ./certs/nginx-mtls/server.crt -noout -subject -issuer -ext subjectAltName
```

##### 7. 통신 프로토콜 및 제어 로직 (Control Lifecycle & Protocol)

###### Control Lifecycle

- **Generate Root CA:** 루트 키와 루트 인증서를 생성합니다.
- **Issue Server Cert:** 게이트웨이 DNS/IP SAN을 포함한 서버 인증서를 발급합니다.
- **Issue Client Certs:** Qt, CCTV, STM32용 클라이언트 인증서를 발급합니다.
- **Verify:** 생성 직후 체인 검증과 SAN 출력을 수행합니다.
- **Deploy:** 생성 산출물을 Kubernetes Secret으로 매핑해 Nginx/Mosquitto에 주입합니다.

###### Command Reference

| 구분 | 명령 | 설명 |
| --- | --- | --- |
| 생성 | `./generate_certs.sh` | 기본 인증서 세트 생성 |
| 검증 | `openssl verify ... server.crt` | 서버 인증서 체인 검증 |
| 검증 | `openssl x509 -in ... -ext subjectAltName` | SAN 확인 |
| 배포 | `kubectl create secret ...` | 생성 파일을 Secret으로 변환 |

###### Stream/Data Format

| 항목 | 형식 |
| --- | --- |
| 루트/서버/클라이언트 인증서 | PEM (`.crt`) |
| 개인키 | PEM (`.key`) |
| CSR | PEM (`.csr`) |
| Secret 입력 | Kubernetes Secret 파일 키-값 |

##### 8. 문제 해결 및 운영 체크리스트 (Troubleshooting & Checklist)

###### Troubleshooting

| 증상 | 원인 | 해결책 |
| --- | --- | --- |
| SAN이 맞지 않음 | DNS/IP 환경변수 오설정 | `GATEWAY_SERVER_DNS`, `GATEWAY_SERVER_IP` 수정 후 재생성 |
| MQTTS Secret이 동작하지 않음 | `rootCA.crt`를 `ca.crt`로 넣지 않음 | `mqtt-certs` 파일명 매핑 재확인 |
| CCTV 제어 인증서가 없음 | 본 스크립트는 Nginx/MQTT용만 생성 | `crow-certs`는 기존 CCTV 인증서 세트 사용 |
| 클라이언트 인증서 검증 실패 | 루트 CA 불일치 또는 파일 손상 | `openssl verify`로 체인 재검증 |

###### Operational Checklist

- `generate_certs.sh`를 실행해 필요한 `.crt`, `.key`가 생성되었는가
- 서버 인증서 SAN이 실제 게이트웨이 주소와 일치하는가
- `nginx-certs`, `mtls-ca`, `mqtt-certs`에 올바른 파일명이 들어갔는가
- `crow-certs`는 별도의 CCTV 제어 인증서 세트로 유지되고 있는가

**작성자:** VEDA Team  
**마지막 업데이트:** 2026-03-19
