from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from tqdm import tqdm

try:
    from ._bootstrap import add_project_root_to_path
except ImportError:
    from _bootstrap import add_project_root_to_path

add_project_root_to_path()

from ml.dataset import generate_all_datasets, input_size_for_window
from ml.evaluate import compare_models
from ml.model import OUTPUT_SIZE, format_architecture, parse_hidden_sizes
from ml.quantize import quantize_model
from ml.train import train_model

SINGLE_HIDDEN_SIZES = (4, 8, 16)
WINDOWS = (2, 3, 4, 5, 6)
SCALE_SHIFT_BYTES = 8
OUTPUT_DIR = Path("artifacts") / "sweep"
DOCS_DIR = Path("docs")


def build_configs() -> list[dict[str, object]]:
    configs: list[dict[str, object]] = []
    for window in WINDOWS:
        for hidden_size in SINGLE_HIDDEN_SIZES:
            configs.append(
                {
                    "name": f"w{window}_h{hidden_size}",
                    "window": window,
                    "hidden_sizes": (hidden_size,),
                    "notes": "one hidden layer",
                }
            )
    configs.append(
        {
            "name": "w8_h32_16",
            "window": 8,
            "hidden_sizes": (32, 16),
            "notes": "two hidden layers",
        }
    )
    return configs


def get_dataset_for_window(
    window: int,
    work_dir: Path,
    args: argparse.Namespace,
    cache: dict[int, dict[str, object]],
) -> dict[str, object]:
    if window not in cache:
        data_dir = work_dir / f"data_w{window}"
        cache[window] = generate_all_datasets(
            data_dir=data_dir,
            summary_path=work_dir / f"dataset_w{window}.json",
            train_games=args.train_games,
            val_games=args.val_games,
            test_games=args.test_games,
            seed=args.seed,
            window=window,
        )
    return cache[window]


def comparison_value(rows: list[dict[str, object]], model_name: str, key: str) -> float:
    row = next(row for row in rows if row["model"] == model_name)
    return float(row[key])


def quantized_size_stats(row: dict[str, object], output_dir: Path) -> dict[str, object]:
    layer_sizes = (int(row["input_size"]), *parse_hidden_sizes(str(row["hidden_sizes"])), OUTPUT_SIZE)
    int8_weight_bytes = sum(layer_sizes[index] * layer_sizes[index + 1] for index in range(len(layer_sizes) - 1))
    int8_bias_bytes = sum(layer_sizes[1:])
    raw_model_bytes = int8_weight_bytes + int8_bias_bytes
    npz_path = output_dir / "work" / str(row["name"]) / "model_quantized.npz"
    return {
        "name": row["name"],
        "architecture": row["architecture"],
        "int8_weight_bytes": int8_weight_bytes,
        "int8_bias_bytes": int8_bias_bytes,
        "raw_model_bytes": raw_model_bytes,
        "stored_array_bytes": raw_model_bytes + SCALE_SHIFT_BYTES,
        "npz_file_bytes": npz_path.stat().st_size,
        "quantized_accuracy": row["quantized_accuracy"],
        "quantized_accuracy_percent": f"{float(row['quantized_accuracy']) * 100:.2f}",
        "epochs_ran": row["epochs_ran"],
    }


def run_config(
    config: dict[str, object],
    work_dir: Path,
    args: argparse.Namespace,
    datasets: dict[int, dict[str, object]],
) -> dict[str, object]:
    name = str(config["name"])
    window = int(config["window"])
    hidden_sizes = tuple(int(size) for size in config["hidden_sizes"])
    data_dir = work_dir / f"data_w{window}"
    model_dir = work_dir / name
    dataset_summary = get_dataset_for_window(window, work_dir, args, datasets)

    metrics = train_model(
        train_csv=data_dir / "train.csv",
        val_csv=data_dir / "val.csv",
        test_csv=data_dir / "test.csv",
        model_path=model_dir / "model_float.pth",
        metrics_path=model_dir / "metrics_float.json",
        epochs=args.epochs,
        batch_size=args.batch_size,
        learning_rate=args.learning_rate,
        seed=args.seed,
        device_name=args.device,
        hidden_sizes=hidden_sizes,
        early_stop_patience=args.early_stop_patience,
        early_stop_min_delta=args.early_stop_min_delta,
    )
    quantize_model(model_dir / "model_float.pth", model_dir / "model_quantized.npz")
    comparison_rows = compare_models(
        test_csv=data_dir / "test.csv",
        float_model_path=model_dir / "model_float.pth",
        quantized_model_path=model_dir / "model_quantized.npz",
        output_json=model_dir / "comparison.json",
        output_csv=model_dir / "comparison.csv",
        batch_size=args.batch_size,
        seed=args.seed,
        device_name=args.device,
    )

    float_accuracy = comparison_value(comparison_rows, "Neural float", "accuracy")
    quantized_accuracy = comparison_value(comparison_rows, "Neural quantized", "accuracy")
    return {
        "name": name,
        "window": window,
        "input_size": input_size_for_window(window),
        "hidden_sizes": ",".join(str(size) for size in hidden_sizes),
        "hidden_layer_count": len(hidden_sizes),
        "architecture": format_architecture(input_size_for_window(window), hidden_sizes),
        "epochs": args.epochs,
        "epochs_ran": metrics["epochs_ran"],
        "stopped_early": metrics["stopped_early"],
        "train_samples": dataset_summary["train"]["samples"],
        "val_samples": dataset_summary["val"]["samples"],
        "test_samples": dataset_summary["test"]["samples"],
        "best_epoch": metrics["best_epoch"],
        "best_val_accuracy": metrics["best_val_accuracy"],
        "random_accuracy": comparison_value(comparison_rows, "Random baseline", "accuracy"),
        "repeat_accuracy": comparison_value(comparison_rows, "Repeat-last-opponent baseline", "accuracy"),
        "float_accuracy": float_accuracy,
        "quantized_accuracy": quantized_accuracy,
        "accuracy_drop": float_accuracy - quantized_accuracy,
        "float_winrate": comparison_value(comparison_rows, "Neural float", "winrate"),
        "quantized_winrate": comparison_value(comparison_rows, "Neural quantized", "winrate"),
        "notes": config["notes"],
    }


def write_results(rows: list[dict[str, object]], output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    json_path = output_dir / "config_results.json"
    csv_path = output_dir / "config_results.csv"
    json_path.write_text(json.dumps(rows, indent=2), encoding="utf-8")

    fieldnames = list(rows[0])
    with csv_path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def plot_results(rows: list[dict[str, object]], output_path: Path) -> None:
    labels = [row["name"] for row in rows]
    float_acc = [float(row["float_accuracy"]) for row in rows]
    quant_acc = [float(row["quantized_accuracy"]) for row in rows]
    positions = np.arange(len(rows))
    width = 0.38

    fig, ax = plt.subplots(figsize=(13, 6))
    ax.bar(positions - width / 2, float_acc, width=width, label="float")
    ax.bar(positions + width / 2, quant_acc, width=width, label="quantized")
    ax.set_xticks(positions, labels=labels, rotation=35, ha="right")
    ax.set_ylim(0.25, 0.70)
    ax.set_ylabel("accuracy")
    ax.grid(True, axis="y", alpha=0.25)
    ax.legend()
    fig.tight_layout()
    fig.savefig(output_path, dpi=160)
    plt.close(fig)


def percent(value: object) -> str:
    return f"{float(value) * 100:.2f}%"


def write_quantized_summary(rows: list[dict[str, object]], output_dir: Path, report_path: Path) -> None:
    summary_rows = sorted(
        (quantized_size_stats(row, output_dir) for row in rows),
        key=lambda row: float(row["quantized_accuracy"]),
        reverse=True,
    )

    csv_path = output_dir / "quantized_model_summary.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=list(summary_rows[0]))
        writer.writeheader()
        writer.writerows(summary_rows)

    best = summary_rows[0]
    best_one_hidden = next(row for row in summary_rows if row["architecture"].count("->") == 2)
    lines = [
        "# Quantized Model Summary",
        "",
        "Raw int8 weights follow `sum(layer_in * layer_out)`. For one hidden layer: `input * hidden + hidden * 3`.",
        "Raw model bytes add int8 biases. Stored array bytes also add two int32 scalars: `scale` and `shift`.",
        "`.npz` file bytes are compressed archive bytes, including npy headers and zip metadata, "
        "so they are larger than raw int8 weights.",
        "",
        "| config | architecture | int8 weights | int8 bias | raw model | npz file | quant acc | epochs ran |",
        "|---|---|---:|---:|---:|---:|---:|---:|",
    ]
    for row in summary_rows:
        lines.append(
            f"| {row['name']} | {row['architecture']} | {row['int8_weight_bytes']} | "
            f"{row['int8_bias_bytes']} | {row['raw_model_bytes']} | {row['npz_file_bytes']} | "
            f"{percent(row['quantized_accuracy'])} | {row['epochs_ran']} |"
        )

    lines.extend(
        [
            "",
            f"Best accuracy: `{best['name']}`, {percent(best['quantized_accuracy'])}, "
            f"{best['int8_weight_bytes']} int8 weight bytes, {best['raw_model_bytes']} raw model bytes.",
            "",
            f"Best one-hidden tradeoff: `{best_one_hidden['name']}`, {percent(best_one_hidden['quantized_accuracy'])}, "
            f"{best_one_hidden['int8_weight_bytes']} int8 weight bytes, "
            f"{best_one_hidden['raw_model_bytes']} raw model bytes.",
        ]
    )
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_report(rows: list[dict[str, object]], report_path: Path) -> None:
    best = max(rows, key=lambda row: float(row["float_accuracy"]))
    best_one_hidden = max(
        (row for row in rows if int(row["hidden_layer_count"]) == 1),
        key=lambda row: float(row["float_accuracy"]),
    )
    max_abs_drop = max(abs(float(row["accuracy_drop"])) for row in rows)
    lines = [
        "# Config Study",
        "",
        "Compared windows 2..6 with one hidden layer sizes 4, 8, 16.",
        "Kept one two-hidden-layer config: window 8, hidden 32/16.",
        "Datasets use doubled default game counts: 680 train, 150 validation, 150 test.",
        f"Each config trains up to {rows[0]['epochs']} epochs with early stopping on validation loss.",
        "",
        f"Best float config: `{best['name']}` with {percent(best['float_accuracy'])} accuracy.",
        f"Best one-hidden config: `{best_one_hidden['name']}` with "
        f"{percent(best_one_hidden['float_accuracy'])} accuracy.",
        f"Largest absolute quantization change: {percent(max_abs_drop)}.",
        "",
        "| config | architecture | epochs ran | float acc | quant acc | drop | repeat baseline | notes |",
        "|---|---|---:|---:|---:|---:|---:|---|",
    ]
    for row in sorted(rows, key=lambda item: float(item["float_accuracy"]), reverse=True):
        lines.append(
            f"| {row['name']} | {row['architecture']} | {row['epochs_ran']} | {percent(row['float_accuracy'])} | "
            f"{percent(row['quantized_accuracy'])} | {percent(row['accuracy_drop'])} | "
            f"{percent(row['repeat_accuracy'])} | {row['notes']} |"
        )
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Compare small MLP configs.")
    parser.add_argument("--epochs", type=int, default=100)
    parser.add_argument("--batch-size", type=int, default=256)
    parser.add_argument("--learning-rate", type=float, default=0.001)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--device", default="auto")
    parser.add_argument("--train-games", type=int, default=680)
    parser.add_argument("--val-games", type=int, default=150)
    parser.add_argument("--test-games", type=int, default=150)
    parser.add_argument("--early-stop-patience", type=int, default=5)
    parser.add_argument("--early-stop-min-delta", type=float, default=0.001)
    return parser.parse_args()


def run_sweep(args: argparse.Namespace, output_dir: Path = OUTPUT_DIR, work_dir: Path | None = None) -> None:
    work_dir = output_dir / "work" if work_dir is None else work_dir
    datasets: dict[int, dict[str, object]] = {}
    rows = []
    for config in tqdm(build_configs(), desc="sweep configs"):
        rows.append(run_config(config, work_dir, args, datasets))

    write_results(rows, output_dir)
    plot_results(rows, output_dir / "config_results.png")
    write_report(rows, DOCS_DIR / "config_study.md")
    write_quantized_summary(rows, output_dir, DOCS_DIR / "quantized_model_summary.md")


def main() -> None:
    run_sweep(parse_args())


if __name__ == "__main__":
    main()
