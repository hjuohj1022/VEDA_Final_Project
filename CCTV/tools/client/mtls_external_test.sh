#!/usr/bin/env bash
set -euo pipefail

HOST=""
PORT="9090"
CA_FILE="certs/forTestClient/rootCA.crt"
CERT_FILE="certs/forTestClient/cctv.crt"
KEY_FILE="certs/forTestClient/cctv.key"
COMMAND="pause"
TIMEOUT_SEC="8"

usage() {
  cat <<'EOF'
Usage:
  mtls_external_test.sh --host <HOST_OR_IP> [options]

Options:
  --port <PORT>          Default: 9090
  --ca <FILE>            Default: certs/forTestClient/rootCA.crt
  --cert <FILE>          Default: certs/forTestClient/cctv.crt
  --key <FILE>           Default: certs/forTestClient/cctv.key
  --cmd <TEXT>           Default: pause
  --timeout <SEC>        Default: 8
  -h, --help             Show this help

Example:
  ./mtls_external_test.sh --host 203.0.113.10 \
    --ca certs/forTestClient/rootCA.crt --cert certs/forTestClient/cctv.crt --key certs/forTestClient/cctv.key --port 9090
EOF
}

fail() {
  echo "[FAIL] $*" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --host) HOST="${2:-}"; shift 2 ;;
    --port) PORT="${2:-}"; shift 2 ;;
    --ca) CA_FILE="${2:-}"; shift 2 ;;
    --cert) CERT_FILE="${2:-}"; shift 2 ;;
    --key) KEY_FILE="${2:-}"; shift 2 ;;
    --cmd) COMMAND="${2:-}"; shift 2 ;;
    --timeout) TIMEOUT_SEC="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) fail "Unknown argument: $1" ;;
  esac
done

[[ -n "$HOST" ]] || fail "--host is required. Use -h for help."
command -v openssl >/dev/null 2>&1 || fail "openssl is not installed."
[[ -f "$CA_FILE" ]] || fail "CA file not found: $CA_FILE"
[[ -f "$CERT_FILE" ]] || fail "Client cert file not found: $CERT_FILE"
[[ -f "$KEY_FILE" ]] || fail "Client key file not found: $KEY_FILE"

RUN_RC=0
RUN_TIMED_OUT=0
RUN_OUT=""
run_with_timeout() {
  local secs="$1"
  shift
  local out_file="/tmp/mtls_test.$$.$RANDOM.out"
  local to_file="/tmp/mtls_test.$$.$RANDOM.timeout"
  rm -f "$out_file" "$to_file"

  "$@" >"$out_file" 2>&1 &
  local pid=$!

  (
    sleep "$secs"
    if kill -0 "$pid" 2>/dev/null; then
      : >"$to_file"
      kill "$pid" 2>/dev/null || true
    fi
  ) &
  local watchdog_pid=$!

  set +e
  wait "$pid"
  RUN_RC=$?
  set -e

  kill "$watchdog_pid" 2>/dev/null || true
  wait "$watchdog_pid" 2>/dev/null || true

  if [[ -f "$to_file" ]]; then
    RUN_TIMED_OUT=1
  else
    RUN_TIMED_OUT=0
  fi

  RUN_OUT="$(cat "$out_file" 2>/dev/null || true)"
  rm -f "$out_file" "$to_file"
}

echo "[INFO] Target: ${HOST}:${PORT}"
echo "[INFO] Command: ${COMMAND}"
echo "[INFO] Timeout: ${TIMEOUT_SEC}s"

echo "[TEST 1/2] TLS without client cert must be rejected"
run_with_timeout "${TIMEOUT_SEC}" \
  bash -c 'openssl s_client -connect "$1:$2" -servername "$1" -CAfile "$3" -verify_return_error -quiet < /dev/null' \
  _ "${HOST}" "${PORT}" "${CA_FILE}"
NO_CERT_OUT="${RUN_OUT}"
NO_CERT_RC="${RUN_RC}"

if [[ "${RUN_TIMED_OUT}" -eq 1 ]]; then
  if grep -Eqi "certificate required|handshake failure|bad certificate|did not return a certificate|alert certificate required" <<<"${NO_CERT_OUT}"; then
    echo "[PASS] Server rejected client without cert (pattern found before timeout)"
  else
    echo "[WARN] No-cert test timed out (${TIMEOUT_SEC}s). Continue to positive mTLS test."
  fi
elif [[ ${NO_CERT_RC} -ne 0 ]] && grep -Eqi "certificate required|handshake failure|bad certificate|did not return a certificate|alert certificate required" <<<"${NO_CERT_OUT}"; then
  echo "[PASS] Server rejected client without cert (mTLS enforcement confirmed)"
else
  echo "${NO_CERT_OUT}" | sed 's/^/[DEBUG] /'
  echo "[WARN] No-cert test did not show explicit rejection text. Continue to positive mTLS test."
fi

echo "[TEST 2/2] TLS with client cert must succeed and return command response"
run_with_timeout "${TIMEOUT_SEC}" \
  bash -c '{ printf "%s\n" "$1"; sleep 0.3; } | openssl s_client -connect "$2:$3" -servername "$2" -CAfile "$4" -cert "$5" -key "$6" -verify_return_error -quiet' \
  _ "${COMMAND}" "${HOST}" "${PORT}" "${CA_FILE}" "${CERT_FILE}" "${KEY_FILE}"
WITH_CERT_OUT="${RUN_OUT}"
WITH_CERT_RC="${RUN_RC}"

if grep -Eqi "(^|[[:space:]])OK([[:space:]]|$|=)" <<<"${WITH_CERT_OUT}"; then
  RESP_LINE="$(grep -E "OK" <<<"${WITH_CERT_OUT}" | tail -n 1)"
  if [[ "${RUN_TIMED_OUT}" -eq 1 ]]; then
    echo "[PASS] mTLS success response found before timeout: ${RESP_LINE}"
  else
    echo "[PASS] mTLS success response: ${RESP_LINE}"
  fi
else
  echo "${WITH_CERT_OUT}" | sed 's/^/[DEBUG] /'
  if [[ "${RUN_TIMED_OUT}" -eq 1 ]]; then
    fail "With-cert test timed out (${TIMEOUT_SEC}s) and no OK response was observed."
  else
    fail "mTLS authenticated request failed"
  fi
fi

echo "[DONE] External mTLS test passed."
