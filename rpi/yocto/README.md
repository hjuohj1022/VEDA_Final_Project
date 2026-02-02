# VEDA RPi4 Yocto Layer

Raspberry Pi 4를 위한 커스텀 Yocto Linux 이미지 및 K3s 클러스터 구축 가이드입니다.

## 1. Build Setup
이 레이어 디렉토리를 `poky` 디렉토리 옆에 위치시키세요.
`conf/local.conf` 설정은 동봉된 `conf/local.conf.sample`을 참조하십시오.

---

## 2. Post-Installation Setup (초기 설정)
이미지를 SD카드에 굽고 부팅한 후, 다음 순서대로 설정을 진행합니다.

### 2-1. Hostname 설정
각 노드(Master/Worker)를 구분하기 위해 호스트 이름을 변경합니다.

```bash
# 1. 호스트 이름 변경 (예: pi-master, pi-worker1)
hostnamectl set-hostname pi-worker1

# 2. 호스트 파일 수정 (127.0.1.1 부분 수정)
nano /etc/hosts

# 3. 재부팅
reboot
```

### 2-2. 사용자 생성 및 권한 부여
보안을 위해 Root 계정 대신 일반 사용자를 생성하고 sudo 권한을 부여합니다. Yocto 이미지 특성상 `/etc/sudoers` 파일의 권한 수정이 선행되어야 합니다.

```bash
# 1. 새 사용자 생성 (예: s01)
useradd -m -s /bin/sh s01
passwd s01

# 2. sudo 그룹 추가 및 사용자 등록
grep sudo /etc/group || groupadd sudo
usermod -aG sudo s01

# 3. sudoers 파일 권한 수정 및 주석 제거
chmod +w /etc/sudoers
nano /etc/sudoers

# [수정 사항] 아래 줄의 주석(#)을 제거하여 sudo 그룹 권한 활성화:
# %sudo ALL=(ALL:ALL) ALL  ->  %sudo ALL=(ALL:ALL) ALL

chmod -w /etc/sudoers
```

### 2-3. 터미널 프롬프트(UX) 개선
`user@hostname:path$` 형태로 프롬프트를 변경하여 식별을 용이하게 합니다.

```bash
nano ~/.bashrc

# 파일 끝에 아래 내용 추가
export PS1='\u@\h:\w\$ '

source ~/.bashrc
```

### 2-4. 보안 설정 (Root SSH 차단)
일반 유저 생성이 완료되면 Root의 원격 접속을 차단합니다.

```bash
nano /etc/ssh/sshd_config
# PermitRootLogin Yes -> No 로 변경

systemctl restart sshd
```

---

## 3. K3s Cluster Installation
Yocto 미니멀 이미지 호환성을 위해 바이너리 경로 생성 후 설치를 진행합니다.

### Master Node
```bash
# 1. 필수 디렉토리 생성 (Yocto 환경 대응)
sudo mkdir -p /usr/local/bin

# 2. 설치
curl -sfL https://get.k3s.io | sh -

# 3. 토큰 확인 (Worker 연결용)
sudo cat /var/lib/rancher/k3s/server/node-token
```

### Worker Node
Master 노드의 IP와 토큰을 사용하여 연결합니다.

```bash
# 1. 필수 디렉토리 생성
sudo mkdir -p /usr/local/bin

# 2. 에이전트 설치 및 연결
# <MASTER_IP>와 <TOKEN>을 실제 값으로 변경하세요.
curl -sfL https://get.k3s.io | K3S_URL=https://<MASTER_IP>:6443 K3S_TOKEN=<TOKEN> sh -
```