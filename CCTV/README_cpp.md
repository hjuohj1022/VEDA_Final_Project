# Depth-Anything-V2 C++ TensorRT 배포 프로젝트 (오늘의 작업 요약)

**날짜:** 2026-02-10

## 1. 주요 이슈 및 해결 (Troubleshooting)

### 🔴 그래픽 드라이버 크래시 및 시스템 프리징 (TDR 발생)
- **원인:** 
  1. 엔진 내 텐서 이름과 소스 코드상의 바인딩 이름이 일치하지 않아 GPU가 잘못된 메모리 영역을 참조함.
  2. RTSP 스트림 지연 시 딜레이 없는 무한 루프(Spin-lock)로 인해 CPU 점유율이 100%로 치솟음.
- **해결:**
  - `engine->getIOTensorName()`을 통해 텐서 이름을 동적으로 조회하여 바인딩하도록 수정.
  - `cap.grab()` 실패 시 `std::this_thread::sleep_for(5ms)`를 추가하여 시스템 자원 보호.

### 🔴 빌드 환경 오류 ("No CUDA toolset found")
- **원인:** CMake의 Visual Studio Generator가 CUDA 도구 모음을 자동으로 찾지 못하는 환경적 결함.
- **해결:** `nvcc` 컴파일러를 직접 호출하는 **Custom Command 방식**으로 `CMakeLists.txt`를 재작성하여 빌드 성공. (링커 유도를 위해 `dummy.cpp` 도입)

## 2. 성능 최적화 (CUDA Kernel 적용)

- **전처리 가속:** 기존 CPU(OpenCV)에서 수행하던 정규화 및 HWC→CHW 변환을 GPU 커널(`main.cu`)로 이관.
- **데이터 전송 효율화:** 
  - CPU -> GPU 전송 데이터를 `float`(3.2MB)에서 `uint8`(0.8MB)로 변경하여 PCIe 대역폭 점유율 4배 감소.
  - 결과적으로 CPU 점유율 대폭 하락 및 안정적인 FPS 확보.

## 3. 보안 및 유지보수 설정 (Config Refactoring)

깃허브 공유 및 로컬 개발 편의성을 위해 설정을 분리했습니다.

- **`local_paths.cmake`**: TensorRT, OpenCV, CUDA의 로컬 설치 경로 관리 (Git 제외)
- **`app_config.h`**: 모델 경로, RTSP URL 등 실행 환경 설정 관리 (Git 제외)
- **`.gitignore`**: 빌드 결과물, 백업 파일(`.bak`), 개인 설정 파일이 커밋되지 않도록 설정.
- **`.example` 파일**: 타 사용자를 위한 설정 가이드용 예시 파일 제공.

---

## 4. 백업 정보
- `main.cpp.bak`: CPU 전처리 버전 백업
- `main.cu.bak`: CUDA 전처리 최적화 버전 백업
