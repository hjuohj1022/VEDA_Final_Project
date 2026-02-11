# Technical Memo: Depth Anything V2 TensorRT 최적화 가이드

이 문서는 PyTorch 모델을 ONNX로 변환하고, 다시 TensorRT 엔진(`.engine`)으로 최적화하여 RTX 3080 Ti 환경에서 실시간 추론(50+ FPS)을 달성하는 과정을 기록합니다.

---

## 1. 개요
- **모델:** Depth Anything V2 (ViT-L / Large)
- **목표:** RTSP 스트리밍 영상의 Depth Map 실시간 생성 및 시각화
- **환경:** Windows 11, RTX 3080 Ti, TensorRT 10.15.1

---

## 2. Step 1: PyTorch → ONNX 변환
PyTorch 모델을 중간 단계인 ONNX 포맷으로 내보냅니다.

### 주요 사항
- **필수 라이브러리:** `pip install onnx`
- **변환 스크립트:** `export_onnx.py` 실행
- **주의사항:** 
  - `TracerWarning`은 모델 내부의 동적 제어 흐름(boolean check 등) 때문에 발생하지만, 고정 입력 크기(518x518)를 사용하므로 무시 가능.
  - `weights_only=True` 설정을 통해 보안 경고 방지 권장.

---

## 3. Step 2: ONNX → TensorRT Engine 변환
NVIDIA TensorRT의 `trtexec.exe` 도구를 사용하여 하드웨어 최적화 엔진을 생성합니다.

### 실행 명령어 (Windows cmd 기준)
```cmd
"C:\path	o\TensorRT-10.15.1.29\bin	rtexec.exe" ^
  --onnx="depth_anything_v2_vitl.onnx" ^
  --saveEngine="depth_anything_v2_vitl.engine" ^
  --fp16
```

### 핵심 포인트
- **--fp16:** RTX 3080 Ti(Ampere 아키텍처)의 Tensor Core를 활용하여 속도를 대폭 향상 (FP32 대비 약 2배).
- **PowerShell 주의:** PowerShell에서 `--` 파라미터를 잘못 해석할 수 있으므로, `cmd /c`를 사용하거나 인자 전체를 따옴표로 감싸서 실행해야 함.
- **결과:** 약 150초의 빌드 시간 소요 후, 53 FPS / 18ms Latency 달성 확인.

---

## 4. Step 3: Python TensorRT 추론 구현
TensorRT 10.x API의 변경된 사항을 반영한 고성능 추론 코드 구현 팁입니다.

### 구현 전략
1. **메모리 관리:** `pycuda` 대신 이미 설치된 `torch`를 사용하여 GPU 메모리를 할당하면 라이브러리 충돌이 적고 안정적임.
2. **고정 바인딩:** 매 프레임 주소를 새로 고치는 대신, `set_tensor_address`로 GPU 버퍼 주소를 고정하고 `copy_()` 메서드로 데이터만 교체하는 것이 빠르고 안정적임.
3. **API 호환성:** TensorRT 10 버전에서는 `execute_v2` 호출 시 바인딩 포인트 리스트(`bindings=[ptr1, ptr2]`)를 명시적으로 전달해야 함.

### 전처리 로직 (고정)
- Input Size: 518x518
- Mean: [0.485, 0.456, 0.406]
- Std: [0.229, 0.224, 0.225]

---

## 5. 트러블슈팅 요약
- **검은 화면 발생 시:** 추론 결과가 GPU에서 CPU로 제대로 복사되지 않았거나, 입출력 텐서 인덱스가 뒤바뀌었는지 확인 필요.
- **Triton/xFormers 경고:** 윈도우 환경에서는 무시해도 성능에 큰 영향 없음.
- **invalid value encountered in cast:** 뎁스값이 전부 0이거나 NaN일 때 발생. `set_tensor_address`와 데이터 복사(`copy_`) 시점 확인으로 해결.

---
**작성일:** 2026-02-09
**작성자:** Catus_KIM
