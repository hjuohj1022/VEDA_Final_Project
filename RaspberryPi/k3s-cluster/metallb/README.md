### 컴포넌트 명칭

MetalLB 설정 디렉터리는 베어메탈 K3s 환경에서 `LoadBalancer` 타입 서비스에 외부 IP를 할당하기 위한 주소 풀과 광고 정책을 정의합니다. 이 프로젝트에서는 주로 `nginx-service`에 외부 진입 IP를 부여하는 역할을 담당합니다.

**주요 환경 및 버전**
- Kubernetes CRD: `IPAddressPool`, `L2Advertisement`
- 네임스페이스: `metallb-system`
- 현재 주소 풀: `<METALLB_ADDRESS_RANGE>`

##### 1. 컴포넌트 구성 요소 및 역할 (Component Overview & Module Map)

###### 아키텍처 유형 (Architectural Pattern)

- **적용 유형:** Bare-Metal LoadBalancer IP Assignment
- **설명:** 클라우드 제공자의 외부 로드밸런서가 없는 환경에서 MetalLB가 `LoadBalancer` 서비스에 실제 LAN IP를 할당합니다. 이 디렉터리는 애플리케이션 서비스 자체가 아니라 외부 진입 주소를 부여하는 인프라 계층입니다.

###### 주요 구성 (Major Components)

| 구성 요소 | 역할 | 파일 |
| --- | --- | --- |
| IPAddressPool | 할당 가능한 외부 IP 범위 정의 | `metallb-config.yaml` |
| L2Advertisement | L2 광고 정책 정의 | `metallb-config.yaml` |

###### 모듈 상세 (Module Detail)

| 파일 | 상세 책임 |
| --- | --- |
| `metallb-config.yaml` | `first-pool` 주소 범위와 `example` 광고 정책을 정의 |

##### 2. 시스템 아키텍처 및 특징 (Architecture & Features)

###### Architecture Diagram

```text
[ MetalLB Controller/Speaker ]
          |
          | assign IP from first-pool
          v
[ nginx-service / LoadBalancer ]
          |
          \--> External Client reaches cluster via assigned LAN IP
```

###### Features

- **기능 1:** `<METALLB_ADDRESS_RANGE>` 범위에서 외부 IP를 할당합니다.
- **기능 2:** `L2Advertisement`로 동일 서브넷에 ARP 기반 노출을 지원합니다.
- **기능 3:** `nginx-service` 같은 외부 진입 서비스가 클러스터 밖에서 직접 접근 가능해집니다.

##### 3. 개발 환경 구축 및 의존성 (Requirements & Dependencies)

###### Requirements

- **사전 설치:** MetalLB controller/speaker가 클러스터에 설치되어 있어야 함
- **네트워크:** 주소 풀이 실제 LAN 대역과 충돌하지 않아야 함
- **대상 서비스:** `type: LoadBalancer` 서비스가 존재해야 함

###### 경로 및 설정 (Path Configurations)

- **핵심 파일**
  - `metallb-config.yaml`
- **네임스페이스**
  - `metallb-system`
- **주소 풀**
  - `<METALLB_ADDRESS_RANGE>`

###### Dependency Setup

MetalLB 본체 설치 후 설정만 적용합니다.

```bash
kubectl apply -f RaspberryPi/k3s-cluster/metallb/metallb-config.yaml
```

##### 4. 설정 가이드 (Configuration)

###### 설정 파일명 1: `metallb-config.yaml`

- `IPAddressPool` 이름: `first-pool`
- `namespace: metallb-system`
- 주소 범위: `<METALLB_ADDRESS_RANGE>`

###### 설정 파일명 2: `L2Advertisement`

- 리소스 이름: `example`
- `ipAddressPools: [first-pool]`

###### 적용 대상

- `nginx-service` 같은 `LoadBalancer` 타입 서비스
- 향후 외부 공개 서비스가 늘어나면 같은 풀 또는 추가 풀에 연결 가능

##### 5. 빌드 및 정적 분석 (Build & Static Analysis)

###### Build Process

이 디렉터리는 빌드 대상이 없으며 CRD manifest만 적용합니다.

```bash
kubectl apply -f RaspberryPi/k3s-cluster/metallb/metallb-config.yaml
```

###### Static Analysis

- `kubectl apply --dry-run=client -f metallb-config.yaml`
- 주소 범위가 DHCP 대역과 충돌하지 않는지 확인
- `metallb-system` 네임스페이스 존재 여부 확인

##### 6. 테스트 및 실행 (Test & Run)

###### Run (실행 방법)

```bash
kubectl get ipaddresspools -n metallb-system
kubectl get l2advertisements -n metallb-system
kubectl get svc nginx-service
```

###### Test (검증 방법)

- `nginx-service`에 `EXTERNAL-IP`가 할당되는지 확인
- 할당된 IP가 같은 네트워크에서 ping 또는 HTTPS로 접근되는지 확인

##### 7. 통신 프로토콜 및 제어 로직 (Control Lifecycle & Protocol)

###### Control Lifecycle

- **Configure:** `IPAddressPool`과 `L2Advertisement`를 등록합니다.
- **Assign:** `LoadBalancer` 서비스가 생성되면 MetalLB가 풀에서 IP를 선택합니다.
- **Advertise:** 선택된 IP를 L2 광고해 외부 장치가 해당 IP를 게이트웨이 진입점으로 사용하게 합니다.

###### Command Reference

| 구분 | 명령 | 설명 |
| --- | --- | --- |
| 적용 | `kubectl apply -f metallb-config.yaml` | MetalLB 설정 반영 |
| 조회 | `kubectl get ipaddresspools -n metallb-system` | 주소 풀 확인 |
| 조회 | `kubectl get l2advertisements -n metallb-system` | 광고 정책 확인 |
| 조회 | `kubectl get svc nginx-service` | 외부 IP 확인 |

###### Stream/Data Format

| 항목 | 형식 |
| --- | --- |
| 설정 파일 | Kubernetes YAML |
| 리소스 종류 | `IPAddressPool`, `L2Advertisement` |
| 주소 표현 | IPv4 범위 문자열 |

##### 8. 문제 해결 및 운영 체크리스트 (Troubleshooting & Checklist)

###### Troubleshooting

| 증상 | 원인 | 해결책 |
| --- | --- | --- |
| `EXTERNAL-IP`가 `pending` | MetalLB controller/speaker 미기동 | `metallb-system` Pod 상태 확인 |
| 외부 IP가 할당되지 않음 | 주소 풀 충돌 또는 서비스 타입 문제 | IP 범위와 `type: LoadBalancer` 확인 |
| IP는 붙었는데 접근 불가 | ARP 광고 또는 노드 네트워크 문제 | L2 네트워크와 노드 NIC 상태 확인 |

###### Operational Checklist

- MetalLB가 클러스터에 설치되어 있는가
- 주소 풀이 실제 사설망 대역과 충돌하지 않는가
- `nginx-service`가 `LoadBalancer` 타입인가
- 할당된 IP가 LAN에서 실제로 접근 가능한가

**작성자:** A.E.G.I.S Team  
**마지막 업데이트:** 2026-03-19
