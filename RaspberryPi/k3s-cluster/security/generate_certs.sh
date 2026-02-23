#!/bin/bash

# 설정
CERT_DIR="./certs"
mkdir -p $CERT_DIR
DAYS=3650 # 10년 유효

echo "🔐 [1/3] Root CA (마스터 키) 생성 중..."
openssl genrsa -out $CERT_DIR/rootCA.key 4096
openssl req -x509 -new -nodes -key $CERT_DIR/rootCA.key -sha256 -days $DAYS -out $CERT_DIR/rootCA.crt \
    -subj "/C=KR/ST=Seoul/L=Veda/O=MyProject/CN=VedaRootCA"

echo "🌐 [2/3] Nginx 서버용 인증서 생성 중... (mTLS 검증용)"
# 참고: 1단계 Certbot 공인 인증서와 별개로, 내부 mTLS 통신을 위한 서버 신분증입니다.
openssl genrsa -out $CERT_DIR/server.key 2048
openssl req -new -key $CERT_DIR/server.key -out $CERT_DIR/server.csr \
    -subj "/C=KR/ST=Seoul/L=Veda/O=MyProject/CN=api.myproject.com"
openssl x509 -req -in $CERT_DIR/server.csr -CA $CERT_DIR/rootCA.crt -CAkey $CERT_DIR/rootCA.key \
    -CAcreateserial -out $CERT_DIR/server.crt -days $DAYS -sha256

echo "📱 [3/3] 기기별 클라이언트 인증서 생성 중..."

# 1. CCTV 기기용 (MQTT 및 mTLS용)
openssl genrsa -out $CERT_DIR/cctv.key 2048
openssl req -new -key $CERT_DIR/cctv.key -out $CERT_DIR/cctv.csr -subj "/C=KR/CN=CCTV-01"
openssl x509 -req -in $CERT_DIR/cctv.csr -CA $CERT_DIR/rootCA.crt -CAkey $CERT_DIR/rootCA.key \
    -CAcreateserial -out $CERT_DIR/cctv.crt -days $DAYS -sha256

# 2. STM32 기기용
openssl genrsa -out $CERT_DIR/stm32.key 2048
openssl req -new -key $CERT_DIR/stm32.key -out $CERT_DIR/stm32.csr -subj "/C=KR/CN=STM32-01"
openssl x509 -req -in $CERT_DIR/stm32.csr -CA $CERT_DIR/rootCA.crt -CAkey $CERT_DIR/rootCA.key \
    -CAcreateserial -out $CERT_DIR/stm32.crt -days $DAYS -sha256

# 3. Qt 클라이언트용 (MQTT 접속용)
openssl genrsa -out $CERT_DIR/client-qt.key 2048
openssl req -new -key $CERT_DIR/client-qt.key -out $CERT_DIR/client-qt.csr -subj "/C=KR/CN=Admin-Qt"
openssl x509 -req -in $CERT_DIR/client-qt.csr -CA $CERT_DIR/rootCA.crt -CAkey $CERT_DIR/rootCA.key \
    -CAcreateserial -out $CERT_DIR/client-qt.crt -days $DAYS -sha256

# 권한 설정
chmod 600 $CERT_DIR/*.key
chmod 644 $CERT_DIR/*.crt

echo "✅ 모든 인증서 발급 완료! ($CERT_DIR 디렉터리 확인)"
ls -l $CERT_DIR
