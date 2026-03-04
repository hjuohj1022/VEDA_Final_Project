import argparse
import json
import os
from typing import Any

import torch
from omegaconf import OmegaConf
from safetensors.torch import load_file


def choose_device(device_arg: str) -> torch.device:
    if device_arg == "cpu":
        return torch.device("cpu")
    if device_arg == "cuda":
        if not torch.cuda.is_available():
            raise RuntimeError("CUDA requested but torch.cuda.is_available() is False.")
        return torch.device("cuda")
    return torch.device("cuda" if torch.cuda.is_available() else "cpu")


def build_da3_model_from_local_dir(model_dir: str) -> torch.nn.Module:
    cfg_path = os.path.join(model_dir, "config.json")
    weight_path = os.path.join(model_dir, "model.safetensors")
    if not os.path.exists(cfg_path):
        raise FileNotFoundError(f"Missing config file: {cfg_path}")
    if not os.path.exists(weight_path):
        raise FileNotFoundError(f"Missing safetensors file: {weight_path}")

    try:
        from depth_anything_3.cfg import create_object
    except Exception as e:
        raise ImportError(
            "Failed to import depth_anything_3.cfg. Ensure package is installed in current Python env."
        ) from e

    with open(cfg_path, "r", encoding="utf-8") as f:
        raw = json.load(f)
    if "config" not in raw:
        raise KeyError(f"Invalid config.json: missing 'config' key ({cfg_path})")

    cfg = OmegaConf.create(raw["config"])
    model = create_object(cfg)
    state_dict = load_file(weight_path)
    target_keys = set(model.state_dict().keys())

    # HF safetensors often stores "model.xxx" keys, while nn.Module expects "xxx".
    remapped = state_dict
    sample_key = next(iter(state_dict.keys()))
    if sample_key.startswith("model."):
        remapped = {k[len("model."):]: v for k, v in state_dict.items()}
    elif sample_key.startswith("module."):
        remapped = {k[len("module."):]: v for k, v in state_dict.items()}

    # Fallback: pick mapping with higher key overlap.
    if not set(remapped.keys()).intersection(target_keys):
        model_stripped = {k[len("model."):]: v for k, v in state_dict.items() if k.startswith("model.")}
        module_stripped = {k[len("module."):]: v for k, v in state_dict.items() if k.startswith("module.")}
        candidates = [state_dict, model_stripped, module_stripped]
        best = max(candidates, key=lambda d: len(set(d.keys()).intersection(target_keys)) if d else -1)
        remapped = best if best else state_dict

    missing, unexpected = model.load_state_dict(remapped, strict=False)
    if missing:
        print(f"[WARN] Missing keys while loading weights: {len(missing)}")
    if unexpected:
        print(f"[WARN] Unexpected keys while loading weights: {len(unexpected)}")
    return model


def extract_depth_tensor(output: Any) -> torch.Tensor:
    if torch.is_tensor(output):
        depth = output
    elif isinstance(output, dict):
        depth = None
        for key in ("depth", "pred_depth", "predicted_depth", "metric_depth", "output"):
            val = output.get(key)
            if torch.is_tensor(val):
                depth = val
                break
        if depth is None:
            raise TypeError(f"Dict output does not contain a tensor depth key. keys={list(output.keys())}")
    elif isinstance(output, (list, tuple)):
        depth = None
        for item in output:
            if torch.is_tensor(item):
                depth = item
                break
        if depth is None:
            raise TypeError("Tuple/list output does not contain a tensor.")
    else:
        raise TypeError(f"Unsupported model output type: {type(output)}")

    if depth.ndim == 3:
        depth = depth.unsqueeze(1)  # [N,H,W] -> [N,1,H,W]
    elif depth.ndim == 2:
        depth = depth.unsqueeze(0).unsqueeze(0)  # [H,W] -> [1,1,H,W]
    elif depth.ndim != 4:
        raise ValueError(f"Unexpected depth tensor ndim={depth.ndim}, expected 2/3/4.")
    return depth.float()


class DepthExportWrapper(torch.nn.Module):
    def __init__(self, core_model: torch.nn.Module):
        super().__init__()
        self.core_model = core_model

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        # DA3 expects [B, N, 3, H, W]. We export with single-view N=1.
        out = self.core_model(x.unsqueeze(1))
        return extract_depth_tensor(out)


def parse_args() -> argparse.Namespace:
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.abspath(os.path.join(script_dir, "..", ".."))
    parser = argparse.ArgumentParser(
        description="Export DA3 Metric safetensors checkpoint to ONNX depth output."
    )
    parser.add_argument(
        "--model-dir",
        default=os.path.join(project_root, "ml_assets", "checkpoints", "DA3METRIC-LARGE"),
        help="Local HF-format DA3 metric model directory containing model.safetensors/config.json",
    )
    parser.add_argument(
        "--output",
        default=os.path.join(project_root, "ml_assets", "onnx", "da3metric_560x1008.onnx"),
        help="Output ONNX file path",
    )
    parser.add_argument("--height", type=int, default=560, help="Input height")
    parser.add_argument("--width", type=int, default=1008, help="Input width")
    parser.add_argument("--opset", type=int, default=17, help="ONNX opset version")
    parser.add_argument(
        "--device",
        choices=["auto", "cpu", "cuda"],
        default="auto",
        help="Export device",
    )
    parser.add_argument(
        "--dynamic",
        action="store_true",
        help="Enable dynamic H/W axes (batch remains fixed at 1)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    device = choose_device(args.device)

    if args.height <= 0 or args.width <= 0:
        raise ValueError("height/width must be positive.")
    if args.height % 14 != 0 or args.width % 14 != 0:
        print(f"[WARN] Recommended DA3 input uses multiples of 14. got={args.height}x{args.width}")

    print(f"[INFO] Loading DA3 metric model from: {args.model_dir}")
    core_model = build_da3_model_from_local_dir(args.model_dir).to(device=device).eval()
    wrapper = DepthExportWrapper(core_model).to(device=device).eval()

    dummy = torch.randn(1, 3, args.height, args.width, device=device, dtype=torch.float32)
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)

    dynamic_axes = None
    if args.dynamic:
        dynamic_axes = {"input": {2: "h", 3: "w"}, "depth": {2: "h", 3: "w"}}

    with torch.no_grad():
        print(f"[INFO] Exporting ONNX: {args.output}")
        torch.onnx.export(
            wrapper,
            dummy,
            args.output,
            opset_version=args.opset,
            input_names=["input"],
            output_names=["depth"],
            dynamic_axes=dynamic_axes,
            do_constant_folding=True,
        )

    print("[INFO] ONNX export done.")
    print(
        "[INFO] Next: trtexec --onnx=<onnx> --saveEngine=<engine> --fp16 "
        "--minShapes=input:1x3:{h}:{w} --optShapes=input:1x3:{h}:{w} --maxShapes=input:1x3:{h}:{w} --verbose".format(
            h=args.height, w=args.width
        )
    )


if __name__ == "__main__":
    main()
