# 🐬 MariaDB Deployment on K3s (Raspberry Pi)

라즈베리파이(Ubuntu 24.04/ARM64) 기반 K3s 클러스터에서 동작하는 MariaDB 배포 설정입니다.
데이터 영속성(PVC)과 보안(Secret) 설정, 그리고 한국 시간(KST) 및 한글 지원 설정이 포함되어 있습니다.

## 📂 파일 구성
* **mariadb-secret.yaml**: DB root 및 사용자 비밀번호 저장 (보안 주의 - 실제 배포 시 생성 필요)
* **mariadb-pvc.yaml**: 데이터 영구 저장을 위한 스토리지 설정 (Local Path)
* **mariadb-deploy.yaml**: Deployment(파드 실행) 및 Service(네트워크 연결) 설정

## 🚀 배포 방법 (Deployment Order)
반드시 아래 순서대로 실행해야 에러가 발생하지 않습니다.

### 1. Secret 생성 (비밀번호)
```bash
kubectl apply -f mariadb-secret.yaml
```

### 2. PVC 생성 (스토리지)
```bash
kubectl apply -f mariadb-pvc.yaml
```

### 3. Deployment & Service 배포
```bash
kubectl apply -f mariadb-deploy.yaml
```

## ✅ 상태 확인
```bash
# 파드 상태 확인 (STATUS: Running 확인)
kubectl get pods -l app=mariadb -o wide

# 로그 확인 (초기화 완료 메시지 'ready for connections' 확인)
kubectl logs -l app=mariadb
```

## 🔌 접속 정보
* **Service Type**: LoadBalancer
* **Internal Port**: 3306
* **External Access**: DBeaver 등의 툴을 사용하여 Node IP로 접속 가능
* **Timezone**: Asia/Seoul (KST)
* **Character Set**: utf8mb4 (한글/이모지 지원)