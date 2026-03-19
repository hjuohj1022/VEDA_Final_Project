### 컴포넌트 명칭

MariaDB는 Crow Server의 계정, 인증, 복구 관련 데이터를 저장하는 내부 데이터베이스 컴포넌트입니다. 이 디렉터리는 MariaDB 컨테이너 이미지, PVC, 초기화 SQL, Secret 예시를 포함하며 K3s 클러스터 내부에서 `mariadb-service:3306`으로 서비스됩니다.

**주요 환경 및 버전**
- 데이터베이스 엔진: `mariadb:10.6`
- 배포 형태: Kubernetes Deployment + ClusterIP Service + PVC
- 문자셋 기본값: `utf8mb4`, `utf8mb4_unicode_ci`

##### 1. 컴포넌트 구성 요소 및 역할 (Component Overview & Module Map)

###### 아키텍처 유형 (Architectural Pattern)

- **적용 유형:** Internal Stateful Database Service
- **설명:** MariaDB는 외부에 직접 공개되지 않는 내부 상태 저장 서비스입니다. Crow Server가 사용자 계정과 2FA 정보를 읽고 쓰는 저장소로 사용하며, PVC를 통해 데이터 지속성을 확보합니다.

###### 주요 구성 (Major Components)

| 구성 요소 | 역할 | 파일 |
| --- | --- | --- |
| DB Container | MariaDB 엔진 실행 | `Dockerfile` |
| Deployment | 컨테이너 실행, Secret/PVC 마운트 | `mariadb-deploy.yaml` |
| Service | 내부 접근용 `ClusterIP` 노출 | `mariadb-deploy.yaml` |
| PVC | `/var/lib/mysql` 영속 스토리지 | `mariadb-pvc.yaml` |
| Init SQL | 기본 테이블과 테스트 계정 생성 | `mariadb-init.yaml` |
| Secret Example | 비밀번호 키 구조 예시 | `mariadb-secret.example.yaml` |

###### 모듈 상세 (Module Detail)

| 파일 | 상세 책임 |
| --- | --- |
| `Dockerfile` | `mariadb:10.6` 기반 이미지와 기본 문자셋 설정 |
| `mariadb-deploy.yaml` | `mariadb` Deployment, `mariadb-service` Service, 환경변수 및 볼륨 연결 |
| `mariadb-pvc.yaml` | `local-path` StorageClass 기반 `1Gi` PVC 정의 |
| `mariadb-init.yaml` | `users` 테이블 생성 및 `test/test` 기본 계정 삽입 |
| `mariadb-secret.example.yaml` | `root-password`, `user-password` 키 예시 제공 |

##### 2. 시스템 아키텍처 및 특징 (Architecture & Features)

###### Architecture Diagram

```text
[ Crow Server ]
      |
      | TCP 3306
      v
[ mariadb-service :3306 / ClusterIP ]
      |
      +--> MariaDB Pod
             |- /var/lib/mysql               <- mariadb-pvc
             \- /docker-entrypoint-initdb.d  <- mariadb-init-sql ConfigMap
```

###### Features

- **기능 1:** `Crow Server` 전용 내부 DB로만 노출되어 외부 직접 접근을 줄입니다.
- **기능 2:** `mariadb-pvc`를 통해 재시작 후에도 데이터가 유지됩니다.
- **기능 3:** `mariadb-init-sql` ConfigMap으로 첫 기동 시 스키마를 자동 초기화합니다.
- **기능 4:** Secret 기반 비밀번호 주입으로 평문 자격증명을 manifest 본문에서 분리합니다.

##### 3. 개발 환경 구축 및 의존성 (Requirements & Dependencies)

###### Requirements

- **클러스터:** K3s 또는 Kubernetes
- **스토리지:** `local-path` StorageClass 사용 가능해야 함
- **시크릿:** `mariadb-secret`
- **상위 의존:** Crow Server가 DB 호스트로 `mariadb-service`를 참조

###### 경로 및 설정 (Path Configurations)

- **핵심 파일**
  - `mariadb-deploy.yaml`
  - `mariadb-pvc.yaml`
  - `mariadb-init.yaml`
  - `mariadb-secret.example.yaml`
- **필수 Secret 키**
  - `root-password`
  - `user-password`
- **고정 노드**
  - `pi-worker2`

###### Dependency Setup

Secret을 먼저 준비한 뒤 PVC와 ConfigMap, Deployment 순으로 적용합니다.

```bash
kubectl apply -f RaspberryPi/k3s-cluster/mariadb/mariadb-pvc.yaml
kubectl apply -f RaspberryPi/k3s-cluster/mariadb/mariadb-init.yaml
kubectl apply -f RaspberryPi/k3s-cluster/mariadb/mariadb-deploy.yaml
```

##### 4. 설정 가이드 (Configuration)

###### 설정 파일명 1: `mariadb-deploy.yaml`

- 컨테이너 이미지: `hjuohj/mariadb-server:latest`
- 내부 포트: `3306/TCP`
- 환경변수
  - `TZ=Asia/Seoul`
  - `MARIADB_DATABASE=veda_db`
  - `MARIADB_USER=veda_user`
  - `MARIADB_ROOT_PASSWORD <- mariadb-secret/root-password`
  - `MARIADB_PASSWORD <- mariadb-secret/user-password`

###### 설정 파일명 2: `mariadb-pvc.yaml`

- `storageClassName: local-path`
- 요청 용량: `1Gi`
- PVC 이름: `mariadb-pvc`

###### 설정 파일명 3: `mariadb-init.yaml`

- `mariadb-init-sql` ConfigMap 생성
- `users` 테이블 초기화
- `INSERT IGNORE INTO users (id, password) VALUES ('test', 'test')`

##### 5. 빌드 및 정적 분석 (Build & Static Analysis)

###### Build Process

이 디렉터리는 공식 MariaDB 이미지를 기반으로 한 간단한 Dockerfile을 포함합니다. 로컬 이미지가 필요하다면 다음처럼 빌드할 수 있습니다.

```bash
docker build -t local/mariadb-server:dev RaspberryPi/k3s-cluster/mariadb
```

Kubernetes 반영은 manifest 적용으로 수행합니다.

```bash
kubectl apply -f RaspberryPi/k3s-cluster/mariadb/mariadb-pvc.yaml
kubectl apply -f RaspberryPi/k3s-cluster/mariadb/mariadb-init.yaml
kubectl apply -f RaspberryPi/k3s-cluster/mariadb/mariadb-deploy.yaml
```

###### Static Analysis

- `kubectl apply --dry-run=client -f mariadb-pvc.yaml`
- `kubectl apply --dry-run=client -f mariadb-deploy.yaml`
- Secret 키 이름과 env 참조 일치 여부 확인

##### 6. 테스트 및 실행 (Test & Run)

###### Run (실행 방법)

```bash
kubectl get pods -l app=mariadb -o wide
kubectl get pvc mariadb-pvc
kubectl get svc mariadb-service
kubectl logs deploy/mariadb
```

###### Test (검증 방법)

- Pod 상태가 `Running`
- PVC 상태가 `Bound`
- `mariadb-service:3306` 생성 확인
- Crow Server에서 DB 연결 로그 이상 여부 확인

필요하면 포트포워딩으로 직접 확인할 수 있습니다.

```bash
kubectl port-forward svc/mariadb-service 3306:3306
```

##### 7. 통신 프로토콜 및 제어 로직 (Control Lifecycle & Protocol)

###### Control Lifecycle

- **Start:** PVC와 ConfigMap, Secret이 준비된 뒤 MariaDB Pod가 기동됩니다.
- **Init:** 빈 데이터 디렉터리일 때 `/docker-entrypoint-initdb.d/init.sql`이 한 번 적용됩니다.
- **Serve:** Crow Server가 `mariadb-service:3306`으로 계정/인증 데이터를 조회합니다.

###### Command Reference

| 구분 | 명령 | 설명 |
| --- | --- | --- |
| 배포 | `kubectl apply -f mariadb-pvc.yaml` | 스토리지 준비 |
| 배포 | `kubectl apply -f mariadb-init.yaml` | 초기 SQL ConfigMap 생성 |
| 배포 | `kubectl apply -f mariadb-deploy.yaml` | DB Pod와 Service 배포 |
| 운영 | `kubectl logs deploy/mariadb` | DB 기동 로그 확인 |
| 운영 | `kubectl port-forward svc/mariadb-service 3306:3306` | 로컬 확인용 포워딩 |

###### Stream/Data Format

| 항목 | 형식 |
| --- | --- |
| 서비스 포트 | `3306/TCP` |
| 인증 방식 | Secret 기반 비밀번호 |
| 초기 데이터 | SQL 스크립트 (`init.sql`) |
| 문자셋 | `utf8mb4` |

##### 8. 문제 해결 및 운영 체크리스트 (Troubleshooting & Checklist)

###### Troubleshooting

| 증상 | 원인 | 해결책 |
| --- | --- | --- |
| Pod가 기동하지 않음 | `mariadb-secret` 누락 또는 키명 불일치 | `root-password`, `user-password` 존재 여부 확인 |
| PVC가 `Pending` | 스토리지 프로비저너 문제 | `local-path` StorageClass와 노드 디스크 상태 확인 |
| 초기 SQL이 적용되지 않음 | 이미 데이터 디렉터리가 생성됨 | PVC 초기화 여부와 `mariadb-init-sql` 마운트 확인 |
| Crow Server DB 연결 실패 | 서비스명/포트 또는 비밀번호 문제 | `mariadb-service`, `MARIADB_*` 값 대조 |

###### Operational Checklist

- `mariadb-secret`가 먼저 생성되었는가
- `mariadb-pvc`가 `Bound` 상태인가
- `pi-worker2` 노드가 실제 존재하는가
- `users` 테이블이 초기화되었는가
- Crow Server가 `mariadb-service:3306`으로 연결 가능한가

**작성자:** VEDA Team  
**마지막 업데이트:** 2026-03-19
