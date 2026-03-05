import requests
import json
import sys

# 테스트 설정
BASE_URL = "https://192.168.55.200"  # Crow 서버 주소
USER_ID = "admin"
USER_PW = "admin123"

# SSL 검증 비활성화 (개발용 자가서명 인증서 사용 시)
requests.packages.urllib3.disable_warnings()

def test_sunapi_proxy():
    print(f"--- Sunapi Proxy Test starting at {BASE_URL} ---")
    
    # 1. 로그인하여 JWT 토큰 획득
    try:
        login_res = requests.post(
            f"{BASE_URL}/login",
            json={"id": USER_ID, "password": USER_PW},
            verify=False
        )
        if login_res.status_code != 200:
            print(f"Login failed: {login_res.status_code} {login_res.text}")
            return
        
        token = login_res.text.strip()
        print(f"Login success. Token: {token[:20]}...")
    except Exception as e:
        print(f"Connection error during login: {e}")
        return

    headers = {
        "Authorization": f"Bearer {token}",
        "Accept": "application/json"
    }

    # 2. Sunapi 프록시 요청 (예: 시스템 정보 조회)
    # 실제 카메라의 stw-cgi/system.cgi?getSubInfo 경로를 호출하게 됨
    proxy_path = "/sunapi/stw-cgi/system.cgi?getSubInfo"
    print(f"\nRequesting Proxy: {proxy_path}")
    
    try:
        res = requests.get(
            f"{BASE_URL}{proxy_path}",
            headers=headers,
            verify=False
        )
        print(f"Proxy Response Status: {res.status_code}")
        print(f"Proxy Response Body:\n{res.text[:500]}") # 너무 길면 잘라서 출력
        
    except Exception as e:
        print(f"Proxy request failed: {e}")

if __name__ == "__main__":
    test_sunapi_proxy()
