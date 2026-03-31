#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace thermal_dtls_gateway {

// 현재 시각을 epoch milliseconds 단위로 반환합니다.
long long currentTimeMs();
// 문자열 양쪽 공백을 제거한 복사본을 반환합니다.
std::string trimCopy(const std::string& value);
// 환경변수가 비어 있거나 없으면 기본값을 반환합니다.
std::string envOrDefault(const char* name, const std::string& fallback);
// 필수 환경변수를 읽고, 값이 없으면 예외를 발생시킵니다.
std::string requireEnv(const char* name);
// 정수형 환경변수를 읽고, 값이 없으면 기본값을 반환합니다.
int envIntOrDefault(const char* name, int fallback);
// big-endian 16비트 값을 host 바이트 순서로 읽습니다.
std::uint16_t readBe16(const unsigned char* p);
// 불리언 환경변수를 읽고, 값이 없으면 기본값을 반환합니다.
bool envBoolOrDefault(const char* name, bool fallback);
// hex 문자열을 바이트 배열로 변환합니다.
std::vector<unsigned char> parseHex(const std::string& hex);
// OpenSSL 에러 스택을 prefix와 함께 출력합니다.
void printOpenSslErrors(const std::string& prefix);

} // namespace thermal_dtls_gateway
