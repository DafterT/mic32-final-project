from __future__ import annotations

import argparse
import json
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
import torch
from torch import nn
from torch.utils.data import DataLoader, TensorDataset
from tqdm import tqdm

from ml.dataset import load_csv_numpy
from ml.game_rules import MOVES
from ml.model import build_model, format_architecture, parse_hidden_sizes
from ml.torch_utils import choose_device, cpu_state_dict, set_seed


def make_loader(csv_path: Path, batch_size: int, shuffle: bool) -> tuple[DataLoader, int]:
    features, targets = load_csv_numpy(csv_path)
    dataset = TensorDataset(torch.from_numpy(features), torch.from_numpy(targets))
    return DataLoader(dataset, batch_size=batch_size, shuffle=shuffle), features.shape[1]


def train_one_epoch(
    model: nn.Module,
    loader: DataLoader,
    criterion: nn.Module,
    optimizer: torch.optim.Optimizer,
    device: torch.device,
    epoch: int,
    epochs: int,
) -> float:
    model.train()
    total_loss = 0.0
    total_samples = 0

    progress = tqdm(loader, desc=f"epoch {epoch}/{epochs}", leave=False)
    for features, targets in progress:
        features = features.to(device)
        targets = targets.to(device)

        optimizer.zero_grad(set_to_none=True)
        logits = model(features)
        loss = criterion(logits, targets)
        loss.backward()
        optimizer.step()

        batch_size = targets.size(0)
        total_loss += loss.item() * batch_size
        total_samples += batch_size
        progress.set_postfix(loss=total_loss / total_samples)

    return total_loss / total_samples


@torch.no_grad()
def evaluate_loader(
    model: nn.Module,
    loader: DataLoader,
    criterion: nn.Module,
    device: torch.device,
) -> tuple[float, float, list[list[int]]]:
    model.eval()
    total_loss = 0.0
    total_correct = 0
    total_samples = 0
    confusion = np.zeros((len(MOVES), len(MOVES)), dtype=np.int64)

    for features, targets in loader:
        features = features.to(device)
        targets = targets.to(device)
        logits = model(features)
        loss = criterion(logits, targets)
        predictions = torch.argmax(logits, dim=1)

        batch_size = targets.size(0)
        total_loss += loss.item() * batch_size
        total_correct += (predictions == targets).sum().item()
        total_samples += batch_size

        for target, prediction in zip(targets.cpu().numpy(), predictions.cpu().numpy()):
            confusion[int(target), int(prediction)] += 1

    return total_loss / total_samples, total_correct / total_samples, confusion.tolist()


@dataclass
class TrainingHistory:
    train_loss: list[float] = field(default_factory=list)
    val_loss: list[float] = field(default_factory=list)
    val_accuracy: list[float] = field(default_factory=list)

    def append(self, train_loss: float, val_loss: float, val_accuracy: float) -> None:
        self.train_loss.append(train_loss)
        self.val_loss.append(val_loss)
        self.val_accuracy.append(val_accuracy)

    def as_dict(self) -> dict[str, list[float]]:
        return {
            "train_loss": self.train_loss,
            "val_loss": self.val_loss,
            "val_accuracy": self.val_accuracy,
        }


@dataclass
class BestModel:
    state: dict[str, torch.Tensor]
    epoch: int = 0
    val_accuracy: float = -1.0

    def update(self, model: nn.Module, epoch: int, val_accuracy: float) -> None:
        if val_accuracy > self.val_accuracy:
            self.val_accuracy = val_accuracy
            self.epoch = epoch
            self.state = cpu_state_dict(model)


@dataclass
class EarlyStopping:
    patience: int
    min_delta: float
    best_val_loss: float = float("inf")
    epochs_without_loss_drop: int = 0
    stopped: bool = False

    def record(self, val_loss: float) -> bool:
        if val_loss < self.best_val_loss - self.min_delta:
            self.best_val_loss = val_loss
            self.epochs_without_loss_drop = 0
        else:
            self.epochs_without_loss_drop += 1

        self.stopped = self.patience > 0 and self.epochs_without_loss_drop >= self.patience
        return self.stopped


def train_model(
    train_csv: Path,
    val_csv: Path,
    test_csv: Path,
    model_path: Path,
    metrics_path: Path,
    epochs: int = 30,
    batch_size: int = 128,
    learning_rate: float = 0.001,
    seed: int = 42,
    device_name: str = "auto",
    hidden_sizes: tuple[int, ...] = (8,),
    early_stop_patience: int = 5,
    early_stop_min_delta: float = 0.001,
) -> dict[str, object]:
    set_seed(seed)
    device = choose_device(device_name)
    criterion = nn.CrossEntropyLoss()

    train_loader, input_size = make_loader(train_csv, batch_size=batch_size, shuffle=True)
    val_loader, _ = make_loader(val_csv, batch_size=batch_size, shuffle=False)
    test_loader, _ = make_loader(test_csv, batch_size=batch_size, shuffle=False)
    model = build_model(input_size=input_size, hidden_sizes=hidden_sizes).to(device)
    optimizer = torch.optim.Adam(model.parameters(), lr=learning_rate)

    history = TrainingHistory()
    best_model = BestModel(state=cpu_state_dict(model))
    early_stopping = EarlyStopping(patience=early_stop_patience, min_delta=early_stop_min_delta)

    for epoch in range(1, epochs + 1):
        train_loss = train_one_epoch(model, train_loader, criterion, optimizer, device, epoch, epochs)
        val_loss, val_accuracy, _confusion = evaluate_loader(model, val_loader, criterion, device)

        history.append(train_loss, val_loss, val_accuracy)
        print(
            f"epoch {epoch:03d}: train_loss={train_loss:.4f} "
            f"val_loss={val_loss:.4f} val_acc={val_accuracy:.4f}"
        )

        best_model.update(model, epoch, val_accuracy)
        if early_stopping.record(val_loss):
            print(
                f"early stop: val_loss did not drop by {early_stop_min_delta:g} "
                f"for {early_stop_patience} epochs"
            )
            break

    model.load_state_dict(best_model.state)
    test_loss, test_accuracy, confusion_matrix = evaluate_loader(model, test_loader, criterion, device)
    history_values = history.as_dict()

    metrics = {
        **history_values,
        "best_epoch": best_model.epoch,
        "best_val_accuracy": best_model.val_accuracy,
        "test_loss": test_loss,
        "test_accuracy": test_accuracy,
        "confusion_matrix": confusion_matrix,
        "device": str(device),
        "epochs": epochs,
        "epochs_ran": len(history_values["train_loss"]),
        "stopped_early": early_stopping.stopped,
        "early_stop_patience": early_stop_patience,
        "early_stop_min_delta": early_stop_min_delta,
        "best_val_loss": early_stopping.best_val_loss,
        "batch_size": batch_size,
        "learning_rate": learning_rate,
        "seed": seed,
        "input_size": input_size,
        "hidden_sizes": list(hidden_sizes),
        "architecture": format_architecture(input_size, hidden_sizes),
    }

    model_path.parent.mkdir(parents=True, exist_ok=True)
    torch.save(
        {
            "model_state_dict": best_model.state,
            "metrics": metrics,
            "input_size": input_size,
            "hidden_sizes": list(hidden_sizes),
        },
        model_path,
    )
    metrics_path.parent.mkdir(parents=True, exist_ok=True)
    metrics_path.write_text(json.dumps(metrics, indent=2), encoding="utf-8")
    return metrics


def main() -> None:
    parser = argparse.ArgumentParser(description="Train float MLP model.")
    parser.add_argument("--train-csv", type=Path, default=Path("data/train.csv"))
    parser.add_argument("--val-csv", type=Path, default=Path("data/val.csv"))
    parser.add_argument("--test-csv", type=Path, default=Path("data/test.csv"))
    parser.add_argument("--model-path", type=Path, default=Path("artifacts/model_float.pth"))
    parser.add_argument("--metrics-path", type=Path, default=Path("artifacts/metrics_float.json"))
    parser.add_argument("--epochs", type=int, default=30)
    parser.add_argument("--batch-size", type=int, default=128)
    parser.add_argument("--learning-rate", type=float, default=0.001)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--device", default="auto")
    parser.add_argument("--hidden-sizes", default="8")
    parser.add_argument("--early-stop-patience", type=int, default=5)
    parser.add_argument("--early-stop-min-delta", type=float, default=0.001)
    args = parser.parse_args()

    train_model(
        train_csv=args.train_csv,
        val_csv=args.val_csv,
        test_csv=args.test_csv,
        model_path=args.model_path,
        metrics_path=args.metrics_path,
        epochs=args.epochs,
        batch_size=args.batch_size,
        learning_rate=args.learning_rate,
        seed=args.seed,
        device_name=args.device,
        hidden_sizes=parse_hidden_sizes(args.hidden_sizes),
        early_stop_patience=args.early_stop_patience,
        early_stop_min_delta=args.early_stop_min_delta,
    )


if __name__ == "__main__":
    main()
