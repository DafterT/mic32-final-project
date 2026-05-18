from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np

from ml.model_io import layer_names_from_state, load_model_state


def quantize_array(values: np.ndarray, scale: int, dtype: np.dtype) -> np.ndarray:
    if dtype == np.int8:
        values = np.clip(np.round(values * scale), -128, 127)
    else:
        values = np.round(values * scale)
    return values.astype(dtype)


def layer_names_from_archive(archive: np.lib.npyio.NpzFile) -> list[str]:
    names: list[str] = []
    index = 1
    while f"fc{index}_weight" in archive.files:
        names.append(f"fc{index}")
        index += 1
    if not names:
        raise ValueError("No fc layers found in quantized archive")
    return names


def quantize_model(
    model_path: Path,
    output_path: Path,
    scale: int = 64,
    shift: int = 6,
) -> dict[str, object]:
    state, _checkpoint = load_model_state(model_path)
    arrays: dict[str, np.ndarray] = {
        "scale": np.array(scale, dtype=np.int32),
        "shift": np.array(shift, dtype=np.int32),
    }

    layer_names = layer_names_from_state(state)
    for layer_name in layer_names:
        weight = state[f"{layer_name}.weight"].detach().cpu().numpy()
        bias = state[f"{layer_name}.bias"].detach().cpu().numpy()
        arrays[f"{layer_name}_weight"] = quantize_array(weight, scale, np.int8)
        arrays[f"{layer_name}_bias"] = quantize_array(bias, scale, np.int8)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(output_path, **arrays)
    return {"path": str(output_path), "scale": scale, "shift": shift, "layers": layer_names}


def quantized_logits(features: np.ndarray, quantized_path: Path) -> np.ndarray:
    with np.load(quantized_path) as archive:
        scale = int(archive["scale"])
        shift = int(archive["shift"])
        layer_names = layer_names_from_archive(archive)
        activations = np.round(features.astype(np.float32) * scale).astype(np.int32)

        for index, layer_name in enumerate(layer_names):
            weight = archive[f"{layer_name}_weight"].astype(np.int32)
            bias = archive[f"{layer_name}_bias"].astype(np.int32)
            logits = (activations @ weight.T) >> shift
            logits = logits + bias
            if index < len(layer_names) - 1:
                activations = np.maximum(logits, 0)
            else:
                return logits

    raise RuntimeError("quantized model has no output layer")


def quantized_predictions(features: np.ndarray, quantized_path: Path) -> np.ndarray:
    return np.argmax(quantized_logits(features, quantized_path), axis=1).astype(np.int64)


def main() -> None:
    parser = argparse.ArgumentParser(description="Quantize float MLP weights.")
    parser.add_argument("--model-path", type=Path, default=Path("artifacts/model_float.pth"))
    parser.add_argument("--output-path", type=Path, default=Path("artifacts/model_quantized.npz"))
    parser.add_argument("--scale", type=int, default=64)
    parser.add_argument("--shift", type=int, default=6)
    args = parser.parse_args()
    quantize_model(args.model_path, args.output_path, scale=args.scale, shift=args.shift)


if __name__ == "__main__":
    main()
