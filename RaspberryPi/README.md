# Raspberry Pi

Raspberry Pi 파트는 두 축으로 구성됩니다. 먼저 `yocto` 디렉터리에서 Raspberry Pi 4용 공통 OS 이미지를 빌드하고, 이후 `k3s-cluster` 디렉터리의 매니페스트를 사용해 여러 장비를 클러스터로 구성하여 서비스를 배포합니다.

## Structure
- `yocto/`
  Raspberry Pi 4용 Yocto 이미지 빌드 및 초기 설정 문서
- `k3s-cluster/`
  K3s 기반 서비스 배포, 아키텍처, 운영 절차 문서
