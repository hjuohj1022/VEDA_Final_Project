# Raspberry Pi

`RaspberryPi` 디렉터리는 VEDA의 Raspberry Pi 4 기반 운영 환경을 다룹니다. 현재 구성은 공통 OS 이미지를 만드는 `yocto` 파트와, 실제 서비스 런타임을 배포하는 `k3s-cluster` 파트로 나뉩니다.

## Structure
| Directory | Role | Main Document |
|---|---|---|
| `yocto/` | Raspberry Pi 4용 공통 Yocto 이미지 빌드와 초기 노드 설정 | `yocto/README.md` |
| `k3s-cluster/` | K3s 클러스터 매니페스트, 네트워크 게이트웨이, 스트리밍, MQTT, DB, 열화상 DTLS 게이트웨이 운영 문서 | `k3s-cluster/README.md` |

## Reading Guide
- Raspberry Pi 노드 이미지를 준비하려면 `yocto/README.md`를 먼저 참고합니다.
- 실제 서비스 배포와 운영 절차, 열화상 스트리밍 경로, 인증서/Secret/ConfigMap 설정은 `k3s-cluster/README.md`를 기준 문서로 사용합니다.
