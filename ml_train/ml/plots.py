from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from ml.game_rules import MOVE_NAMES, MOVES


def plot_training_metrics(metrics: dict[str, object], output_path: Path) -> None:
    epochs = np.arange(1, len(metrics["train_loss"]) + 1)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    fig, ax_loss = plt.subplots(figsize=(9, 5))
    ax_loss.plot(epochs, metrics["train_loss"], label="train loss", color="tab:blue")
    ax_loss.plot(epochs, metrics["val_loss"], label="val loss", color="tab:orange")
    ax_loss.set_xlabel("epoch")
    ax_loss.set_ylabel("loss")
    ax_loss.grid(True, alpha=0.25)

    ax_acc = ax_loss.twinx()
    ax_acc.plot(epochs, metrics["val_accuracy"], label="val accuracy", color="tab:green")
    ax_acc.set_ylabel("accuracy")
    ax_acc.set_ylim(0.0, 1.0)

    lines = ax_loss.get_lines() + ax_acc.get_lines()
    fig.legend(lines, [line.get_label() for line in lines], loc="upper center", ncol=3)
    fig.tight_layout(rect=(0, 0, 1, 0.92))
    fig.savefig(output_path, dpi=160)
    plt.close(fig)


def plot_confusion_matrix(matrix: list[list[int]], output_path: Path, title: str) -> None:
    labels = tuple(MOVE_NAMES[move] for move in MOVES)
    values = np.array(matrix, dtype=np.int64)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    fig, ax = plt.subplots(figsize=(5.5, 5))
    image = ax.imshow(values, cmap="Blues")
    ax.set_title(title)
    ax.set_xlabel("predicted")
    ax.set_ylabel("target")
    ax.set_xticks(range(3), labels=labels)
    ax.set_yticks(range(3), labels=labels)

    for row in range(3):
        for column in range(3):
            ax.text(column, row, str(values[row, column]), ha="center", va="center", color="black")

    fig.colorbar(image, ax=ax, fraction=0.046, pad=0.04)
    fig.tight_layout()
    fig.savefig(output_path, dpi=160)
    plt.close(fig)


def plot_comparison(rows: list[dict[str, object]], output_path: Path) -> None:
    names = [str(row["model"]) for row in rows]
    accuracy = [float(row["accuracy"]) for row in rows]
    winrate = [float(row["winrate"]) for row in rows]
    positions = np.arange(len(names))
    width = 0.36
    output_path.parent.mkdir(parents=True, exist_ok=True)

    fig, ax = plt.subplots(figsize=(10, 5))
    ax.bar(positions - width / 2, accuracy, width=width, label="accuracy")
    ax.bar(positions + width / 2, winrate, width=width, label="winrate")
    ax.set_xticks(positions, labels=names, rotation=15, ha="right")
    ax.set_ylim(0.0, 1.0)
    ax.grid(True, axis="y", alpha=0.25)
    ax.legend()
    fig.tight_layout()
    fig.savefig(output_path, dpi=160)
    plt.close(fig)
