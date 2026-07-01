from __future__ import annotations

import argparse
import csv
import shutil
import tempfile
from pathlib import Path

import torch
from PIL import Image
from torch.utils.data import DataLoader, Dataset

from esp_ppq.api import espdl_quantize_onnx


REPO_ROOT = Path(__file__).resolve().parents[1]
MODEL_DIR = REPO_ROOT / "ai_models" / "drone_cls_pretrained_v3"
IMAGENET_MEAN = torch.tensor([0.485, 0.456, 0.406], dtype=torch.float32).view(3, 1, 1)
IMAGENET_STD = torch.tensor([0.229, 0.224, 0.225], dtype=torch.float32).view(3, 1, 1)


class ImageNetCalibrationDataset(Dataset):
    def __init__(self, paths: list[Path]) -> None:
        self.paths = paths

    def __len__(self) -> int:
        return len(self.paths)

    def __getitem__(self, index: int) -> torch.Tensor:
        image = center_crop_square(Image.open(self.paths[index]).convert("RGB")).resize((128, 128), Image.BILINEAR)
        data = torch.frombuffer(bytearray(image.tobytes()), dtype=torch.uint8)
        data = data.view(128, 128, 3).permute(2, 0, 1).to(torch.float32).div_(255.0)
        return (data - IMAGENET_MEAN) / IMAGENET_STD


def center_crop_square(image: Image.Image) -> Image.Image:
    width, height = image.size
    side = min(width, height)
    left = (width - side) // 2
    top = (height - side) // 2
    return image.crop((left, top, left + side, top + side))


def collect_images(data_root: Path, limit: int) -> list[Path]:
    suffixes = {".bmp", ".jpg", ".jpeg", ".png"}
    paths = sorted(path for path in data_root.rglob("*") if path.suffix.lower() in suffixes)
    return paths[:limit]


def collect_prediction_images(csv_path: Path, limit: int) -> list[Path]:
    if not csv_path.exists():
        return []

    paths: list[Path] = []
    with csv_path.open("r", newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            path_text = row.get("path")
            if not path_text:
                continue
            path = Path(path_text)
            if path.exists():
                paths.append(path)
            if len(paths) >= limit:
                break
    return paths


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Quantize the v3 drone classifier to ESP-DL INT8.")
    parser.add_argument("--onnx", type=Path, default=MODEL_DIR / "drone_cls.onnx")
    parser.add_argument("--out", type=Path, default=MODEL_DIR / "drone_cls_p4_int8.espdl")
    parser.add_argument("--data-root", type=Path, default=Path(r"D:\CAP"))
    parser.add_argument("--predictions", type=Path, default=MODEL_DIR / "dataset_predictions.csv")
    parser.add_argument("--limit", type=int, default=192)
    parser.add_argument("--steps", type=int, default=96)
    parser.add_argument("--target", default="esp32p4")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    paths = collect_images(args.data_root, args.limit)
    if not paths:
        paths = collect_prediction_images(args.predictions, args.limit)
    if not paths:
        raise SystemExit(f"No calibration images found under {args.data_root} or {args.predictions}")

    dataset = ImageNetCalibrationDataset(paths)
    loader = DataLoader(dataset, batch_size=1, shuffle=False, num_workers=0)
    steps = min(args.steps, len(dataset))

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="drone_cls_espdl_") as tmp_dir:
        tmp_onnx = Path(tmp_dir) / args.onnx.name
        shutil.copy2(args.onnx, tmp_onnx)
        espdl_quantize_onnx(
            onnx_import_file=str(tmp_onnx),
            espdl_export_file=str(args.out),
            calib_dataloader=loader,
            calib_steps=steps,
            input_shape=[1, 3, 128, 128],
            target=args.target,
            num_of_bits=8,
            device="cpu",
            error_report=False,
            export_config=True,
            export_test_values=False,
            verbose=0,
            metadata_props={
                "preprocess": "RGB resize 128x128; scale 0..1; ImageNet mean/std; NCHW",
                "classes": "0=NODRONE,1=DRONE",
            },
        )

    print(f"Wrote {args.out} from {len(paths)} calibration images, steps={steps}")


if __name__ == "__main__":
    main()
