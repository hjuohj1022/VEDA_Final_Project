#!/usr/bin/env bash
set -euo pipefail

CERT_DIR="${CERT_DIR:-./certs}"
DAYS="${DAYS:-3650}"

# nginx is the public TLS endpoint for HTTPS, RTSPS and MQTT-over-TLS.
GATEWAY_DIR="$CERT_DIR/nginx-mtls"
GATEWAY_SERVER_DNS="${GATEWAY_SERVER_DNS:-veda.team3.com}"
GATEWAY_SERVER_IP="${GATEWAY_SERVER_IP:-192.168.55.200}"

mkdir -p "$GATEWAY_DIR"

issue_root_ca() {
    local out_dir="$1"
    local common_name="$2"

    openssl genrsa -out "$out_dir/rootCA.key" 4096
    MSYS_NO_PATHCONV=1 openssl req -x509 -new -nodes \
        -key "$out_dir/rootCA.key" \
        -sha256 -days "$DAYS" \
        -out "$out_dir/rootCA.crt" \
        -subj "/C=KR/ST=Seoul/L=Veda/O=MyProject/CN=$common_name" \
        -addext "basicConstraints=critical,CA:TRUE" \
        -addext "keyUsage=critical,keyCertSign,cRLSign" \
        -addext "subjectKeyIdentifier=hash"
}

issue_server_cert() {
    local out_dir="$1"
    local stem="$2"
    local common_name="$3"
    local dns_name="$4"
    local ip_addr="$5"

    openssl genrsa -out "$out_dir/$stem.key" 2048
    MSYS_NO_PATHCONV=1 openssl req -new \
        -key "$out_dir/$stem.key" \
        -out "$out_dir/$stem.csr" \
        -subj "/C=KR/ST=Seoul/L=Veda/O=MyProject/CN=$common_name"

    cat > "$out_dir/${stem}_ext.cnf" <<EOF
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=@alt_names

[alt_names]
DNS.1=$dns_name
IP.1=$ip_addr
EOF

    openssl x509 -req \
        -in "$out_dir/$stem.csr" \
        -CA "$out_dir/rootCA.crt" \
        -CAkey "$out_dir/rootCA.key" \
        -CAcreateserial \
        -out "$out_dir/$stem.crt" \
        -days "$DAYS" \
        -sha256 \
        -extfile "$out_dir/${stem}_ext.cnf"
}

issue_client_cert() {
    local out_dir="$1"
    local stem="$2"
    local common_name="$3"

    openssl genrsa -out "$out_dir/$stem.key" 2048
    MSYS_NO_PATHCONV=1 openssl req -new \
        -key "$out_dir/$stem.key" \
        -out "$out_dir/$stem.csr" \
        -subj "/C=KR/ST=Seoul/L=Veda/O=MyProject/CN=$common_name"

    cat > "$out_dir/${stem}_ext.cnf" <<EOF
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=clientAuth
EOF

    openssl x509 -req \
        -in "$out_dir/$stem.csr" \
        -CA "$out_dir/rootCA.crt" \
        -CAkey "$out_dir/rootCA.key" \
        -CAcreateserial \
        -out "$out_dir/$stem.crt" \
        -days "$DAYS" \
        -sha256 \
        -extfile "$out_dir/${stem}_ext.cnf"
}

echo "[1/3] Generate nginx/mqtt mTLS certificates"
issue_root_ca "$GATEWAY_DIR" "VedaNginxRootCA"
issue_server_cert "$GATEWAY_DIR" "server" "$GATEWAY_SERVER_DNS" "$GATEWAY_SERVER_DNS" "$GATEWAY_SERVER_IP"
issue_client_cert "$GATEWAY_DIR" "client-qt" "Admin-Qt"
issue_client_cert "$GATEWAY_DIR" "client-cctv" "CCTV-01"
issue_client_cert "$GATEWAY_DIR" "client-stm32" "STM32-01"

echo "[2/3] Fix permissions"
chmod 600 "$GATEWAY_DIR"/*.key
chmod 644 "$GATEWAY_DIR"/*.crt

echo "[3/3] Verify output"
openssl verify -CAfile "$GATEWAY_DIR/rootCA.crt" "$GATEWAY_DIR/server.crt"
openssl verify -CAfile "$GATEWAY_DIR/rootCA.crt" "$GATEWAY_DIR/client-qt.crt"

echo
echo "Generated certificate layout:"
echo "  $GATEWAY_DIR/"
echo "    rootCA.crt         -> nginx/mqtt trust root"
echo "    server.crt         -> nginx HTTPS/MQTT/RTSPS server cert"
echo "    client-qt.crt      -> browser/Qt/MQTT client cert"
echo "    client-cctv.crt    -> CCTV device MQTT client cert"
echo "    client-stm32.crt   -> STM32 MQTT client cert"
echo
echo "CCTV control certificates are not generated here."
echo "Use the existing cctv-control certificate set you already issued."
echo
echo "Secret mapping examples:"
echo "  nginx-certs <- $GATEWAY_DIR/server.crt + $GATEWAY_DIR/server.key"
echo "  mtls-ca     <- $GATEWAY_DIR/rootCA.crt"
echo "  crow-certs  <- existing cctv-control rootCA.crt + client-crow.crt + client-crow.key"
echo
echo "Server SAN checks:"
openssl x509 -in "$GATEWAY_DIR/server.crt" -noout -subject -issuer -ext subjectAltName
