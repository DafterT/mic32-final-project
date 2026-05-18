from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path

try:
    from ._bootstrap import add_project_root_to_path
except ImportError:
    from _bootstrap import add_project_root_to_path

add_project_root_to_path()

from ml.dataset import WINDOW, generate_all_datasets, input_size_for_window
from ml.evaluate import compare_models
from ml.model import format_architecture, parse_hidden_sizes
from ml.plots import plot_comparison, plot_confusion_matrix, plot_training_metrics
from ml.quantize import quantize_model
from ml.train import train_model

RANDOM_MODEL = "Random baseline"
REPEAT_MODEL = "Repeat-last-opponent baseline"
FLOAT_MODEL = "Neural float"
QUANTIZED_MODEL = "Neural quantized"
SCALE = 64
SHIFT = 6


@dataclass(frozen=True)
class PipelinePaths:
    data_dir: Path = Path("data")
    artifacts_dir: Path = Path("artifacts")
    docs_dir: Path = Path("docs")

    @property
    def train_csv(self) -> Path:
        return self.data_dir / "train.csv"

    @property
    def val_csv(self) -> Path:
        return self.data_dir / "val.csv"

    @property
    def test_csv(self) -> Path:
        return self.data_dir / "test.csv"

    @property
    def float_model(self) -> Path:
        return self.artifacts_dir / "model_float.pth"

    @property
    def quantized_model(self) -> Path:
        return self.artifacts_dir / "model_quantized.npz"


def format_percent(value: float) -> str:
    return f"{value * 100:.2f}%"


def comparison_row(rows: list[dict[str, object]], model_name: str) -> dict[str, object]:
    return next(row for row in rows if row["model"] == model_name)


def write_report(
    report_path: Path,
    dataset_summary: dict[str, object],
    float_metrics: dict[str, object],
    comparison_rows: list[dict[str, object]],
    window: int,
    hidden_sizes: tuple[int, ...],
) -> None:
    random_acc = float(comparison_row(comparison_rows, RANDOM_MODEL)["accuracy"])
    repeat_acc = float(comparison_row(comparison_rows, REPEAT_MODEL)["accuracy"])
    float_acc = float(comparison_row(comparison_rows, FLOAT_MODEL)["accuracy"])
    quant_acc = float(comparison_row(comparison_rows, QUANTIZED_MODEL)["accuracy"])
    accuracy_drop = float_acc - quant_acc

    lines = [
        "# Results",
        "",
        "## Task",
        "",
        f"Predict next player move from last {window} rounds. "
        f"Input size: {input_size_for_window(window)}. "
        f"Model: MLP {format_architecture(input_size_for_window(window), hidden_sizes)}.",
        "",
        "## Data",
        "",
    ]
    for split, info in dataset_summary.items():
        lines.append(f"- {split}: {info['games']} games, {info['samples']} samples")

    lines.extend(
        [
            "",
            "## Training",
            "",
            f"- device: {float_metrics['device']}",
            f"- epochs: {float_metrics['epochs']}",
            f"- best epoch: {float_metrics['best_epoch']}",
            f"- best val accuracy: {format_percent(float(float_metrics['best_val_accuracy']))}",
            f"- test accuracy: {format_percent(float(float_metrics['test_accuracy']))}",
            "",
            "## Comparison",
            "",
            "| model | accuracy | winrate | notes |",
            "|---|---:|---:|---|",
        ]
    )
    for row in comparison_rows:
        lines.append(
            f"| {row['model']} | {format_percent(float(row['accuracy']))} | "
            f"{format_percent(float(row['winrate']))} | {row['notes']} |"
        )

    lines.extend(
        [
            "",
            "## Conclusions",
            "",
            f"- Neural float better than Random baseline: {float_acc > random_acc}.",
            f"- Neural float better than Repeat-last-opponent baseline: {float_acc > repeat_acc}.",
            f"- Neural quantized accuracy drop: {format_percent(accuracy_drop)}.",
            f"- Quantization acceptable at <=3pp drop: {accuracy_drop <= 0.03}.",
            "",
            "## Artifacts",
            "",
            "- `artifacts/metrics_float.png`",
            "- `artifacts/confusion_float.png`",
            "- `artifacts/comparison.png`",
            "- `artifacts/model_float.pth`",
            "- `artifacts/model_quantized.npz`",
        ]
    )

    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_quantized_metrics(metrics_path: Path, comparison_rows: list[dict[str, object]]) -> None:
    float_row = comparison_row(comparison_rows, FLOAT_MODEL)
    quantized_row = comparison_row(comparison_rows, QUANTIZED_MODEL)
    metrics = {
        "test_accuracy": quantized_row["accuracy"],
        "winrate": quantized_row["winrate"],
        "confusion_matrix": quantized_row["confusion_matrix"],
        "scale": SCALE,
        "shift": SHIFT,
        "accuracy_float": float_row["accuracy"],
        "accuracy_drop": float_row["accuracy"] - quantized_row["accuracy"],
    }
    metrics_path.parent.mkdir(parents=True, exist_ok=True)
    metrics_path.write_text(json.dumps(metrics, indent=2), encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run full ML experiment pipeline.")
    parser.add_argument("--epochs", type=int, default=30)
    parser.add_argument("--batch-size", type=int, default=128)
    parser.add_argument("--learning-rate", type=float, default=0.001)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--device", default="auto")
    parser.add_argument("--train-games", type=int, default=340)
    parser.add_argument("--val-games", type=int, default=75)
    parser.add_argument("--test-games", type=int, default=75)
    parser.add_argument("--window", type=int, default=WINDOW)
    parser.add_argument("--hidden-sizes", default="8")
    parser.add_argument("--early-stop-patience", type=int, default=5)
    parser.add_argument("--early-stop-min-delta", type=float, default=0.001)
    return parser.parse_args()


def run_pipeline(args: argparse.Namespace, paths: PipelinePaths = PipelinePaths()) -> None:
    hidden_sizes = parse_hidden_sizes(args.hidden_sizes)

    dataset_summary = generate_all_datasets(
        data_dir=paths.data_dir,
        summary_path=paths.artifacts_dir / "dataset_summary.json",
        train_games=args.train_games,
        val_games=args.val_games,
        test_games=args.test_games,
        seed=args.seed,
        window=args.window,
    )

    float_metrics = train_model(
        train_csv=paths.train_csv,
        val_csv=paths.val_csv,
        test_csv=paths.test_csv,
        model_path=paths.float_model,
        metrics_path=paths.artifacts_dir / "metrics_float.json",
        epochs=args.epochs,
        batch_size=args.batch_size,
        learning_rate=args.learning_rate,
        seed=args.seed,
        device_name=args.device,
        hidden_sizes=hidden_sizes,
        early_stop_patience=args.early_stop_patience,
        early_stop_min_delta=args.early_stop_min_delta,
    )
    plot_training_metrics(float_metrics, paths.artifacts_dir / "metrics_float.png")
    plot_confusion_matrix(float_metrics["confusion_matrix"], paths.artifacts_dir / "confusion_float.png", FLOAT_MODEL)

    quantize_model(
        model_path=paths.float_model,
        output_path=paths.quantized_model,
        scale=SCALE,
        shift=SHIFT,
    )

    comparison_rows = compare_models(
        test_csv=paths.test_csv,
        float_model_path=paths.float_model,
        quantized_model_path=paths.quantized_model,
        output_json=paths.artifacts_dir / "comparison.json",
        output_csv=paths.artifacts_dir / "comparison.csv",
        batch_size=args.batch_size,
        seed=args.seed,
        device_name=args.device,
    )
    plot_comparison(comparison_rows, paths.artifacts_dir / "comparison.png")
    write_quantized_metrics(paths.artifacts_dir / "metrics_quantized.json", comparison_rows)

    write_report(
        paths.docs_dir / "results.md",
        dataset_summary,
        float_metrics,
        comparison_rows,
        args.window,
        hidden_sizes,
    )


def main() -> None:
    run_pipeline(parse_args())


if __name__ == "__main__":
    main()
