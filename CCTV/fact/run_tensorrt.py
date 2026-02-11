import cv2
import tensorrt as trt
import torch
import numpy as np
import time
import os

# ==========================================
# ★ [설정] 경로 확인
# ==========================================
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
ENGINE_PATH = os.path.join(BASE_DIR, "depth_anything_v2_vitl.engine")
RTSP_URL = "rtsp://insert.your.ip.address:port/ch"
INPUT_SIZE = 518 
# ==========================================

class TRTInference:
    def __init__(self, engine_path):
        self.logger = trt.Logger(trt.Logger.WARNING)
        
        if not os.path.exists(engine_path):
            raise FileNotFoundError(f"❌ 엔진 파일 없음: {engine_path}")

        # 1. 엔진 로드
        with open(engine_path, "rb") as f, trt.Runtime(self.logger) as runtime:
            self.engine = runtime.deserialize_cuda_engine(f.read())
        
        self.context = self.engine.create_execution_context()
        
        # 2. 입출력 텐서 고정 할당
        self.tensors = {}
        self.bindings = []
        self.input_name = None
        self.output_name = None

        for i in range(self.engine.num_io_tensors):
            name = self.engine.get_tensor_name(i)
            shape = self.engine.get_tensor_shape(name)
            dtype = trt.nptype(self.engine.get_tensor_dtype(name))
            
            # GPU 메모리 미리 할당 (고정 주소)
            gpu_mem = torch.empty(tuple(shape), dtype=torch.from_numpy(np.empty(0, dtype=dtype)).dtype, device='cuda')
            
            self.tensors[name] = gpu_mem
            self.bindings.append(gpu_mem.data_ptr())
            
            # TensorRT 10 주소 고정
            self.context.set_tensor_address(name, gpu_mem.data_ptr())

            if self.engine.get_tensor_mode(name) == trt.TensorIOMode.INPUT:
                self.input_name = name
            else:
                self.output_name = name

    def infer(self, image):
        # 1. 전처리
        resized = cv2.resize(image, (INPUT_SIZE, INPUT_SIZE))
        img = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
        img = (img - [0.485, 0.456, 0.406]) / [0.229, 0.224, 0.225]
        img = img.transpose(2, 0, 1)
        img = np.ascontiguousarray(img)
        
        # 2. 데이터를 고정된 GPU 입력 버퍼로 복사
        input_data = torch.from_numpy(img).unsqueeze(0).to('cuda')
        self.tensors[self.input_name].copy_(input_data)
        
        # 3. 추론 실행 (주소 리스트 전달)
        self.context.execute_v2(self.bindings)

        # 4. 결과 반환
        return self.tensors[self.output_name].cpu().numpy()[0]

def run():
    print(f"🚀 TensorRT 실행 (Debug 모드)...")
    try:
        model = TRTInference(ENGINE_PATH)
    except Exception as e:
        print(f"❌ 초기화 실패: {e}")
        return

    print("✅ 준비 완료. 창이 뜨면 뎁스맵 값을 확인하세요.")

    os.environ["OPENCV_FFMPEG_CAPTURE_OPTIONS"] = "rtsp_transport;tcp"
    cap = cv2.VideoCapture(RTSP_URL, cv2.CAP_FFMPEG)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

    prev_time = time.time()

    while True:
        cap.grab()
        ret, frame = cap.read()
        if not ret: continue

        # 추론
        depth = model.infer(frame)

        # [디버그] 값 확인 (터미널에 출력)
        d_min, d_max = depth.min(), depth.max()
        # 만약 이 값이 (0.000 - 0.000) 이면 추론 자체가 안 되는 중입니다.
        # print(f"\rDepth Range: {d_min:.3f} ~ {d_max:.3f} | FPS: {1/(time.time()-prev_time+1e-5):.1f}", end="")

        # 시각화
        depth_norm = (depth - d_min) / (d_max - d_min + 1e-5) * 255.0
        depth_color = cv2.applyColorMap(depth_norm.astype(np.uint8), cv2.COLORMAP_INFERNO)
        
        res_f = cv2.resize(frame, (640, 480))
        res_d = cv2.resize(depth_color, (640, 480))
        combined = np.hstack((res_f, res_d))

        curr_time = time.time()
        fps = 1 / (curr_time - prev_time)
        prev_time = curr_time
        cv2.putText(combined, f"FPS: {fps:.1f}", (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)

        cv2.imshow("Depth Anything V2 (TensorRT Fixed)", combined)
        if cv2.waitKey(1) == ord('q'): break

    cap.release()
    cv2.destroyAllWindows()

if __name__ == '__main__':
    run()
