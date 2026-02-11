import torch
import os
from depth_anything_v2.dpt import DepthAnythingV2

# ==========================================
# [설정] Large 모델 + 경로
# ==========================================
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
MODEL_PATH = os.path.join(BASE_DIR, 'checkpoints', 'depth_anything_v2_vitl.pth')
OUTPUT_ONNX = os.path.join(BASE_DIR, 'depth_anything_v2_vitl.onnx')

def export():
    # 1. 모델 로드 (Large)
    print("🚀 모델 로딩 중...")
    model_configs = {'vitl': {'encoder': 'vitl', 'features': 256, 'out_channels': [256, 512, 1024, 1024]}}
    model = DepthAnythingV2(**model_configs['vitl'])
    model.load_state_dict(torch.load(MODEL_PATH, map_location='cpu'))
    model.eval()

    # 2. 더미 데이터 (3080Ti TensorRT 최적화를 위해 518x518 고정 추천)
    dummy_input = torch.randn(1, 3, 518, 518)

    # 3. 변환
    print(f"📦 ONNX 변환 시작: {OUTPUT_ONNX}")
    torch.onnx.export(
        model, 
        dummy_input, 
        OUTPUT_ONNX, 
        opset_version=11, 
        input_names=['input'], 
        output_names=['depth']
    )
    print("✅ 변환 완료! 이제 TensorRT 변환 단계로 넘어가세요.")

if __name__ == '__main__':
    export()