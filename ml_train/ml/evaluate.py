from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

import numpy as np
import torch
from torch.utils.data import DataLoader, TensorDataset

from ml.dataset import FEATURES_PER_ROUND, load_csv_numpy
from ml.game_rules import MOVES, RESULT_WIN, counter_move, result_for_player
from ml.model import build_model
from ml.model_io import load_model_state, model_config_from_state
from ml.quantize import quantized_predictions
from ml.torch_utils import choose_device


def confusion_matrix(targets: np.ndarray, predictions: np.ndarray) -> list[list[int]]:
    matrix = np.zeros((len(MOVES), len(MOVES)), dtype=np.int64)
    for target, prediction in zip(targets, predictions):
        matrix[int(target), int(prediction)] += 1
    return matrix.tolist()


def accuracy(targets: np.ndarray, predictions: np.ndarray) -> float:
    return float(np.mean(targets == predictions))


def counter_winrate(targets: np.ndarray, predictions: np.ndarray) -> float:
    wins = 0
    for target, prediction in zip(targets, predictions):
        our_move = counter_move(int(prediction))
        wins += result_for_player(our_move, int(target)) == RESULT_WIN
    return wins / len(targets)


def random_baseline(count: int, seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    return rng.integers(0, len(MOVES), size=count, dtype=np.int64)


def repeat_last_opponent_baseline(features: np.ndarray) -> np.ndarray:
    window = features.shape[1] // FEATURES_PER_ROUND
    last_round_start = (window - 1) * FEATURES_PER_ROUND
    last_player_move = features[:, last_round_start : last_round_start + 3]
    return np.argmax(last_player_move, axis=1).astype(np.int64)


@torch.no_grad()
def float_model_predictions(
    model_path: Path,
    features: np.ndarray,
    batch_size: int,
    device_name: str = "auto",
) -> np.ndarray:
    device = choose_device(device_name)
    state, checkpoint = load_model_state(model_path, map_location=device)
    input_size, hidden_sizes = model_config_from_state(state, checkpoint)
    model = build_model(input_size=input_size, hidden_sizes=hidden_sizes).to(device)
    model.load_state_dict(state)
    model.eval()

    dataset = TensorDataset(torch.from_numpy(features))
    loader = DataLoader(dataset, batch_size=batch_size, shuffle=False)
    predictions: list[np.ndarray] = []
    for (batch_features,) in loader:
        logits = model(batch_features.to(device))
        predictions.append(torch.argmax(logits, dim=1).cpu().numpy())
    return np.concatenate(predictions).astype(np.int64)


def metric_row(name: str, targets: np.ndarray, predictions: np.ndarray, notes: str) -> dict[str, object]:
    return {
        "model": name,
        "accuracy": accuracy(targets, predictions),
        "winrate": counter_winrate(targets, predictions),
        "confusion_matrix": confusion_matrix(targets, predictions),
        "notes": notes,
    }


def compare_models(
    test_csv: Path,
    float_model_path: Path,
    quantized_model_path: Path,
    output_json: Path,
    output_csv: Path,
    batch_size: int = 128,
    seed: int = 42,
    device_name: str = "auto",
) -> list[dict[str, object]]:
    features, targets = load_csv_numpy(test_csv)
    rows = [
        metric_row(
            "Random baseline",
            targets,
            random_baseline(len(targets), seed),
            "uniform random class",
        ),
        metric_row(
            "Repeat-last-opponent baseline",
            targets,
            repeat_last_opponent_baseline(features),
            "uses last player move from input history",
        ),
    ]

    float_predictions = float_model_predictions(float_model_path, features, batch_size, device_name)
    rows.append(metric_row("Neural float", targets, float_predictions, "PyTorch logits argmax"))

    quant_predictions = quantized_predictions(features, quantized_model_path)
    rows.append(metric_row("Neural quantized", targets, quant_predictions, "int8 weights/biases, SCALE=64"))

    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(rows, indent=2), encoding="utf-8")

    output_csv.parent.mkdir(parents=True, exist_ok=True)
    with output_csv.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=("model", "accuracy", "winrate", "notes"))
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row[key] for key in ("model", "accuracy", "winrate", "notes")})

    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description="Compare baselines, float model, quantized model.")
    parser.add_argument("--test-csv", type=Path, default=Path("data/test.csv"))
    parser.add_argument("--float-model", type=Path, default=Path("artifacts/model_float.pth"))
    parser.add_argument("--quantized-model", type=Path, default=Path("artifacts/model_quantized.npz"))
    parser.add_argument("--output-json", type=Path, default=Path("artifacts/comparison.json"))
    parser.add_argument("--output-csv", type=Path, default=Path("artifacts/comparison.csv"))
    parser.add_argument("--batch-size", type=int, default=128)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--device", default="auto")
    args = parser.parse_args()

    compare_models(
        test_csv=args.test_csv,
        float_model_path=args.float_model,
        quantized_model_path=args.quantized_model,
        output_json=args.output_json,
        output_csv=args.output_csv,
        batch_size=args.batch_size,
        seed=args.seed,
        device_name=args.device,
    )


if __name__ == "__main__":
    main()
