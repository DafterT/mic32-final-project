from __future__ import annotations

from pathlib import Path

import torch


def load_model_state(
    model_path: Path,
    map_location: str | torch.device = "cpu",
) -> tuple[dict[str, torch.Tensor], dict[str, object]]:
    checkpoint = torch.load(model_path, map_location=map_location, weights_only=False)
    if "model_state_dict" in checkpoint:
        return checkpoint["model_state_dict"], checkpoint
    return checkpoint, {}


def layer_names_from_state(state: dict[str, torch.Tensor]) -> list[str]:
    names: list[str] = []
    index = 1
    while f"fc{index}.weight" in state:
        names.append(f"fc{index}")
        index += 1
    if not names:
        raise ValueError("No fc layers found in model state")
    return names


def model_config_from_state(
    state: dict[str, torch.Tensor],
    checkpoint: dict[str, object],
) -> tuple[int, tuple[int, ...]]:
    input_size = int(checkpoint.get("input_size", state["fc1.weight"].shape[1]))
    layer_names = layer_names_from_state(state)
    hidden_sizes = tuple(int(state[f"{name}.weight"].shape[0]) for name in layer_names[:-1])
    return input_size, hidden_sizes
