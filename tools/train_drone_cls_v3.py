from __future__ import annotations

import argparse
import csv
import json
import random
import shutil
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

import torch
from PIL import Image
from torch import nn
from torch.utils.data import DataLoader, Dataset
from torchvision import models, transforms


REPO_ROOT = Path(__file__).resolve().parents[1]
MODEL_DIR = REPO_ROOT / "ai_models" / "drone_cls_pretrained_v3"
CLASS_NAMES = ["NODRONE", "DRONE"]
CLASS_TO_INDEX = {"NODRONE": 0, "DRONE": 1}
IMAGE_SUFFIXES = {".bmp", ".jpg", ".jpeg", ".png"}
IMAGENET_MEAN = (0.485, 0.456, 0.406)
IMAGENET_STD = (0.229, 0.224, 0.225)


@dataclass(frozen=True)
class Sample:
    path: Path
    label: int
    split: str


class DroneDataset(Dataset):
    def __init__(self, samples: list[Sample], transform) -> None:
        self.samples = samples
        self.transform = transform

    def __len__(self) -> int:
        return len(self.samples)

    def __getitem__(self, index: int) -> tuple[torch.Tensor, int, str]:
        sample = self.samples[index]
        image = Image.open(sample.path).convert("RGB")
        return self.transform(image), sample.label, str(sample.path)


def center_crop_square(image: Image.Image) -> Image.Image:
    width, height = image.size
    side = min(width, height)
    left = (width - side) // 2
    top = (height - side) // 2
    return image.crop((left, top, left + side, top + side))


def collect_class_samples(data_root: Path) -> dict[str, list[Path]]:
    by_class: dict[str, list[Path]] = {}
    for class_name in CLASS_NAMES:
        class_dir = data_root / class_name
        if not class_dir.exists():
            raise SystemExit(f"Missing class directory: {class_dir}")
        paths = sorted(path for path in class_dir.rglob("*") if path.suffix.lower() in IMAGE_SUFFIXES)
        if not paths:
            raise SystemExit(f"No images found in {class_dir}")
        by_class[class_name] = paths
    return by_class


def split_samples(data_root: Path, seed: int, val_ratio: float, test_ratio: float) -> list[Sample]:
    rng = random.Random(seed)
    samples: list[Sample] = []
    for class_name, paths in collect_class_samples(data_root).items():
        paths = list(paths)
        rng.shuffle(paths)
        count = len(paths)
        test_count = max(1, int(round(count * test_ratio)))
        val_count = max(1, int(round(count * val_ratio)))
        if val_count + test_count >= count:
            raise SystemExit(f"Not enough images in {data_root / class_name}: {count}")
        label = CLASS_TO_INDEX[class_name]
        test_paths = paths[:test_count]
        val_paths = paths[test_count:test_count + val_count]
        train_paths = paths[test_count + val_count:]
        samples.extend(Sample(path, label, "test") for path in test_paths)
        samples.extend(Sample(path, label, "val") for path in val_paths)
        samples.extend(Sample(path, label, "train") for path in train_paths)
    return samples


def make_transforms(image_size: int) -> tuple[transforms.Compose, transforms.Compose]:
    train_tf = transforms.Compose([
        transforms.Lambda(center_crop_square),
        transforms.Resize((image_size, image_size)),
        transforms.RandomApply([
            transforms.ColorJitter(brightness=0.18, contrast=0.18, saturation=0.12, hue=0.02),
        ], p=0.55),
        transforms.RandomAffine(degrees=5, translate=(0.03, 0.03), scale=(0.94, 1.06)),
        transforms.RandomHorizontalFlip(p=0.5),
        transforms.ToTensor(),
        transforms.Normalize(IMAGENET_MEAN, IMAGENET_STD),
    ])
    eval_tf = transforms.Compose([
        transforms.Lambda(center_crop_square),
        transforms.Resize((image_size, image_size)),
        transforms.ToTensor(),
        transforms.Normalize(IMAGENET_MEAN, IMAGENET_STD),
    ])
    return train_tf, eval_tf


def build_model(num_classes: int) -> nn.Module:
    model = models.mobilenet_v3_small(weights=None)
    in_features = model.classifier[-1].in_features
    model.classifier[-1] = nn.Linear(in_features, num_classes)
    return model


def checkpoint_state(checkpoint) -> dict[str, torch.Tensor]:
    if isinstance(checkpoint, dict):
        for key in ("model_state", "state_dict", "model"):
            value = checkpoint.get(key)
            if isinstance(value, dict):
                return value
        if all(torch.is_tensor(value) for value in checkpoint.values()):
            return checkpoint
    return {}


def load_partial_checkpoint(model: nn.Module, init_from: Path, device: torch.device) -> int:
    if not init_from.exists():
        return 0
    checkpoint = torch.load(init_from, map_location=device)
    source = checkpoint_state(checkpoint)
    if not source:
        return 0
    target = model.state_dict()
    filtered = {}
    for key, value in source.items():
        clean_key = key[7:] if key.startswith("module.") else key
        if clean_key in target and target[clean_key].shape == value.shape:
            filtered[clean_key] = value
    if not filtered:
        return 0
    target.update(filtered)
    model.load_state_dict(target)
    return len(filtered)


def backup_existing_outputs(out_dir: Path) -> Path | None:
    files = [
        "best.pt",
        "classes.json",
        "confusion_matrix.png",
        "dataset_predictions.csv",
        "drone_cls.onnx",
        "metrics.json",
        "无人机识别混淆矩阵.png",
    ]
    existing = [out_dir / name for name in files if (out_dir / name).exists()]
    if not existing:
        return None
    backup_dir = out_dir / ("backup_" + time.strftime("%Y%m%d_%H%M%S"))
    backup_dir.mkdir(parents=True, exist_ok=True)
    for path in existing:
        shutil.copy2(path, backup_dir / path.name)
    return backup_dir


def class_weights(samples: Iterable[Sample], device: torch.device) -> torch.Tensor:
    counts = [0 for _ in CLASS_NAMES]
    for sample in samples:
        counts[sample.label] += 1
    total = sum(counts)
    weights = [total / (len(CLASS_NAMES) * max(1, count)) for count in counts]
    return torch.tensor(weights, dtype=torch.float32, device=device)


def accuracy_from_logits(logits: torch.Tensor, labels: torch.Tensor) -> float:
    pred = torch.argmax(logits, dim=1)
    return (pred == labels).to(torch.float32).mean().item()


def run_epoch(model: nn.Module,
              loader: DataLoader,
              criterion: nn.Module,
              optimizer: torch.optim.Optimizer | None,
              device: torch.device) -> tuple[float, float]:
    training = optimizer is not None
    model.train(training)
    total_loss = 0.0
    total_acc = 0.0
    total_items = 0
    context = torch.enable_grad() if training else torch.no_grad()
    with context:
        for images, labels, _paths in loader:
            images = images.to(device)
            labels = labels.to(device)
            if training:
                optimizer.zero_grad(set_to_none=True)
            logits = model(images)
            loss = criterion(logits, labels)
            if training:
                loss.backward()
                optimizer.step()
            batch = labels.size(0)
            total_loss += loss.item() * batch
            total_acc += accuracy_from_logits(logits.detach(), labels) * batch
            total_items += batch
    return total_loss / max(1, total_items), total_acc / max(1, total_items)


def predict(model: nn.Module,
            loader: DataLoader,
            device: torch.device) -> tuple[list[int], list[int], list[list[float]], list[str]]:
    model.eval()
    true_labels: list[int] = []
    pred_labels: list[int] = []
    probs: list[list[float]] = []
    paths: list[str] = []
    with torch.no_grad():
        for images, labels, batch_paths in loader:
            images = images.to(device)
            logits = model(images)
            batch_probs = torch.softmax(logits, dim=1).cpu()
            true_labels.extend(labels.tolist())
            pred_labels.extend(torch.argmax(batch_probs, dim=1).tolist())
            probs.extend(batch_probs.tolist())
            paths.extend(batch_paths)
    return true_labels, pred_labels, probs, paths


def classification_report_dict(true_labels: list[int], pred_labels: list[int]) -> dict:
    try:
        from sklearn.metrics import classification_report
        return classification_report(
            true_labels,
            pred_labels,
            labels=[0, 1],
            target_names=CLASS_NAMES,
            output_dict=True,
            zero_division=0,
        )
    except Exception:
        report: dict[str, dict[str, float] | float] = {}
        correct = sum(1 for true, pred in zip(true_labels, pred_labels) if true == pred)
        for index, class_name in enumerate(CLASS_NAMES):
            tp = sum(1 for true, pred in zip(true_labels, pred_labels) if true == index and pred == index)
            fp = sum(1 for true, pred in zip(true_labels, pred_labels) if true != index and pred == index)
            fn = sum(1 for true, pred in zip(true_labels, pred_labels) if true == index and pred != index)
            precision = tp / max(1, tp + fp)
            recall = tp / max(1, tp + fn)
            f1 = 0.0 if precision + recall == 0.0 else (2.0 * precision * recall) / (precision + recall)
            report[class_name] = {
                "precision": precision,
                "recall": recall,
                "f1-score": f1,
                "support": float(sum(1 for true in true_labels if true == index)),
            }
        report["accuracy"] = correct / max(1, len(true_labels))
        return report


def confusion_matrix(true_labels: list[int], pred_labels: list[int]) -> list[list[int]]:
    matrix = [[0, 0], [0, 0]]
    for true, pred in zip(true_labels, pred_labels):
        matrix[true][pred] += 1
    return matrix


def save_confusion_matrix(matrix: list[list[int]], out_path: Path) -> None:
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(5.2, 4.5))
    ax.imshow(matrix, cmap="Blues")
    ax.set_xticks([0, 1], CLASS_NAMES)
    ax.set_yticks([0, 1], CLASS_NAMES)
    ax.set_xlabel("Predicted")
    ax.set_ylabel("True")
    for y in range(2):
        for x in range(2):
            ax.text(x, y, str(matrix[y][x]), ha="center", va="center", color="black", fontsize=14)
    fig.tight_layout()
    fig.savefig(out_path, dpi=180)
    plt.close(fig)


def save_predictions(csv_path: Path,
                     split: str,
                     true_labels: list[int],
                     pred_labels: list[int],
                     probs: list[list[float]],
                     paths: list[str]) -> None:
    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["path", "split", "true", "pred", "nodrone_prob", "drone_prob"])
        for path, true, pred, prob in zip(paths, true_labels, pred_labels, probs):
            writer.writerow([
                path,
                split,
                CLASS_NAMES[true],
                CLASS_NAMES[pred],
                f"{prob[0]:.8f}",
                f"{prob[1]:.8f}",
            ])


def export_onnx(model: nn.Module, out_path: Path, image_size: int, device: torch.device) -> None:
    model.eval()
    dummy = torch.randn(1, 3, image_size, image_size, device=device)
    torch.onnx.export(
        model,
        dummy,
        out_path,
        input_names=["input"],
        output_names=["logits"],
        opset_version=13,
        do_constant_folding=True,
        dynamo=False,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Retrain the ESP32-P4 drone classifier from D:\\CAP.")
    parser.add_argument("--data-root", type=Path, default=Path(r"D:\CAP"))
    parser.add_argument("--out-dir", type=Path, default=MODEL_DIR)
    parser.add_argument("--init-from", type=Path, default=MODEL_DIR / "best.pt")
    parser.add_argument("--epochs", type=int, default=18)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--image-size", type=int, default=128)
    parser.add_argument("--lr", type=float, default=2e-4)
    parser.add_argument("--weight-decay", type=float, default=1e-4)
    parser.add_argument("--val-ratio", type=float, default=0.15)
    parser.add_argument("--test-ratio", type=float, default=0.10)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--num-workers", type=int, default=0)
    parser.add_argument("--no-backup", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    torch.manual_seed(args.seed)
    random.seed(args.seed)

    samples = split_samples(args.data_root, args.seed, args.val_ratio, args.test_ratio)
    train_samples = [sample for sample in samples if sample.split == "train"]
    val_samples = [sample for sample in samples if sample.split == "val"]
    test_samples = [sample for sample in samples if sample.split == "test"]
    print(f"Dataset: train={len(train_samples)} val={len(val_samples)} test={len(test_samples)}")

    train_tf, eval_tf = make_transforms(args.image_size)
    train_loader = DataLoader(
        DroneDataset(train_samples, train_tf),
        batch_size=args.batch_size,
        shuffle=True,
        num_workers=args.num_workers,
    )
    val_loader = DataLoader(
        DroneDataset(val_samples, eval_tf),
        batch_size=args.batch_size,
        shuffle=False,
        num_workers=args.num_workers,
    )
    test_loader = DataLoader(
        DroneDataset(test_samples, eval_tf),
        batch_size=args.batch_size,
        shuffle=False,
        num_workers=args.num_workers,
    )

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = build_model(len(CLASS_NAMES)).to(device)
    loaded = load_partial_checkpoint(model, args.init_from, device)
    if loaded:
        print(f"Loaded {loaded} tensors from {args.init_from}")
    else:
        print(f"No compatible checkpoint loaded from {args.init_from}")

    criterion = nn.CrossEntropyLoss(weight=class_weights(train_samples, device))
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=args.weight_decay)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=max(1, args.epochs))

    history = []
    best_state = None
    best_val_acc = -1.0
    for epoch in range(1, args.epochs + 1):
        train_loss, train_acc = run_epoch(model, train_loader, criterion, optimizer, device)
        val_loss, val_acc = run_epoch(model, val_loader, criterion, None, device)
        scheduler.step()
        history.append({
            "epoch": epoch,
            "train_loss": train_loss,
            "train_acc": train_acc,
            "val_loss": val_loss,
            "val_acc": val_acc,
        })
        print(
            f"epoch {epoch:02d}/{args.epochs} "
            f"train_loss={train_loss:.4f} train_acc={train_acc:.4f} "
            f"val_loss={val_loss:.4f} val_acc={val_acc:.4f}"
        )
        if val_acc > best_val_acc:
            best_val_acc = val_acc
            best_state = {key: value.detach().cpu().clone() for key, value in model.state_dict().items()}

    if best_state is not None:
        model.load_state_dict(best_state)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    backup_dir = None if args.no_backup else backup_existing_outputs(args.out_dir)
    if backup_dir is not None:
        print(f"Backed up previous model files to {backup_dir}")

    true_labels, pred_labels, probs, paths = predict(model, test_loader, device)
    report = classification_report_dict(true_labels, pred_labels)
    matrix = confusion_matrix(true_labels, pred_labels)

    torch.save({
        "arch": "mobilenet_v3_small",
        "model_state": model.state_dict(),
        "classes": CLASS_NAMES,
        "image_size": args.image_size,
        "history": history,
        "test_report": report,
        "confusion_matrix": matrix,
    }, args.out_dir / "best.pt")
    export_onnx(model, args.out_dir / "drone_cls.onnx", args.image_size, device)
    save_predictions(args.out_dir / "dataset_predictions.csv", "test", true_labels, pred_labels, probs, paths)
    save_confusion_matrix(matrix, args.out_dir / "confusion_matrix.png")
    shutil.copy2(args.out_dir / "confusion_matrix.png", args.out_dir / "无人机识别混淆矩阵.png")
    with (args.out_dir / "classes.json").open("w", encoding="utf-8") as handle:
        json.dump({"classes": CLASS_NAMES, "nodrone": 0, "drone": 1}, handle, indent=2, ensure_ascii=False)
    with (args.out_dir / "metrics.json").open("w", encoding="utf-8") as handle:
        json.dump({"history": history, "test_report": report, "confusion_matrix": matrix}, handle, indent=2)

    print("Test report:")
    print(json.dumps(report, indent=2))
    print(f"Wrote model artifacts to {args.out_dir}")


if __name__ == "__main__":
    main()
