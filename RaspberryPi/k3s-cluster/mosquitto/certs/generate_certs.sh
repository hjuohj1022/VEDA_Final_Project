#!/bin/bash

# CA (Certificate Authority) 생성
openssl req -new -x509 -days 3650 -extensions v3_ca -keyout ca.key -out ca.crt -subj "/CN=MyRootCA" -nodes

# Server (Mosquitto) 키 및 CSR 생성
openssl req -new -out server.csr -keyout server.key -subj "/CN=mqtt-server" -nodes

# Server 인증서 서명 (CA로 서명)
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out server.crt -days 3650

# Client 1 (CCTV) 키 및 CSR 생성
openssl req -new -out client-cctv.csr -keyout client-cctv.key -subj "/CN=cctv-01" -nodes

# Client 1 (CCTV) 인증서 서명 (CA로 서명)
openssl x509 -req -in client-cctv.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out client-cctv.crt -days 3650

# Client 2 (Qt App/Admin) 키 및 CSR 생성
openssl req -new -out client-qt.csr -keyout client-qt.key -subj "/CN=admin" -nodes

# Client 2 (Qt App/Admin) 인증서 서명 (CA로 서명)
openssl x509 -req -in client-qt.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out client-qt.crt -days 3650

# 불필요한 파일 삭제
rm *.csr *.srl

echo "Certificates generated successfully!"
