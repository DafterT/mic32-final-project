from __future__ import annotations

import torch
from torch import nn

from ml.dataset import INPUT_SIZE

OUTPUT_SIZE = 3
DEFAULT_HIDDEN_SIZES = (8,)


def parse_hidden_sizes(text: str) -> tuple[int, ...]:
    hidden_sizes = tuple(int(part.strip()) for part in text.split(",") if part.strip())
    if not hidden_sizes or len(hidden_sizes) > 2:
        raise ValueError("Use one hidden layer, or two only for original baseline, example: 8 or 32,16")
    return hidden_sizes


def format_architecture(input_size: int, hidden_sizes: tuple[int, ...]) -> str:
    sizes = [input_size, *hidden_sizes, OUTPUT_SIZE]
    return " -> ".join(str(size) for size in sizes)


MODEL_ARCHITECTURE = format_architecture(INPUT_SIZE, DEFAULT_HIDDEN_SIZES)


class MovePredictor(nn.Module):
    def __init__(self, input_size: int = INPUT_SIZE, hidden_sizes: tuple[int, ...] = DEFAULT_HIDDEN_SIZES) -> None:
        super().__init__()
        if len(hidden_sizes) not in {1, 2}:
            raise ValueError("MovePredictor supports one hidden layer, plus original two-layer baseline.")

        self.hidden_sizes = hidden_sizes
        self.fc1 = nn.Linear(input_size, hidden_sizes[0])
        self.relu1 = nn.ReLU()
        if len(hidden_sizes) == 1:
            self.fc2 = nn.Linear(hidden_sizes[0], OUTPUT_SIZE)
        else:
            self.fc2 = nn.Linear(hidden_sizes[0], hidden_sizes[1])
            self.relu2 = nn.ReLU()
            self.fc3 = nn.Linear(hidden_sizes[1], OUTPUT_SIZE)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = self.relu1(self.fc1(x))
        if len(self.hidden_sizes) == 2:
            x = self.relu2(self.fc2(x))
            return self.fc3(x)
        return self.fc2(x)


def build_model(input_size: int = INPUT_SIZE, hidden_sizes: tuple[int, ...] = DEFAULT_HIDDEN_SIZES) -> MovePredictor:
    return MovePredictor(input_size=input_size, hidden_sizes=hidden_sizes)
