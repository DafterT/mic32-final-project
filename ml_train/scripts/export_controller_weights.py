from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

import numpy as np

FEATURES_PER_ROUND = 6
OUTPUT_SIZE = 3
REQUIRED_ARRAYS = ("scale", "shift", "fc1_weight", "fc1_bias", "fc2_weight", "fc2_bias")
MODEL_ARRAYS = ("fc1_weight", "fc1_bias", "fc2_weight", "fc2_bias")


@dataclass(frozen=True)
class ControllerModel:
    window: int
    input_size: int
    hidden_size: int
    scale: int
    shift: int
    fc1_weight: np.ndarray
    fc1_bias: np.ndarray
    fc2_weight: np.ndarray
    fc2_bias: np.ndarray

    @property
    def raw_bytes(self) -> int:
        return int(self.fc1_weight.size + self.fc1_bias.size + self.fc2_weight.size + self.fc2_bias.size)


def require_int8(name: str, values: np.ndarray) -> None:
    if values.dtype != np.int8:
        raise ValueError(f"{name} must be int8, got {values.dtype}")


def require_arrays(model_path: Path, archive: np.lib.npyio.NpzFile) -> None:
    missing = [name for name in REQUIRED_ARRAYS if name not in archive.files]
    if missing:
        raise ValueError(f"Missing arrays in {model_path}: {missing}")
    if "fc3_weight" in archive.files or "fc3_bias" in archive.files:
        raise ValueError("Controller export supports one hidden layer only")


def validate_model_arrays(
    fc1_weight: np.ndarray,
    fc1_bias: np.ndarray,
    fc2_weight: np.ndarray,
    fc2_bias: np.ndarray,
) -> tuple[int, int, int]:
    for name, values in zip(MODEL_ARRAYS, (fc1_weight, fc1_bias, fc2_weight, fc2_bias)):
        require_int8(name, values)

    if fc1_weight.ndim != 2 or fc2_weight.ndim != 2:
        raise ValueError("Weight arrays must be 2D")
    hidden_size, input_size = fc1_weight.shape
    output_size, fc2_input_size = fc2_weight.shape
    if input_size % FEATURES_PER_ROUND != 0:
        raise ValueError(f"Input size {input_size} is not divisible by {FEATURES_PER_ROUND}")
    if output_size != OUTPUT_SIZE:
        raise ValueError(f"Output size must be {OUTPUT_SIZE}, got {output_size}")
    if fc2_input_size != hidden_size:
        raise ValueError("fc2 input size must match hidden size")
    if fc1_bias.shape != (hidden_size,) or fc2_bias.shape != (OUTPUT_SIZE,):
        raise ValueError("Bias shapes do not match weight shapes")
    return input_size // FEATURES_PER_ROUND, input_size, hidden_size


def load_one_hidden_model(model_path: Path) -> ControllerModel:
    with np.load(model_path) as archive:
        require_arrays(model_path, archive)
        fc1_weight = archive["fc1_weight"]
        fc1_bias = archive["fc1_bias"]
        fc2_weight = archive["fc2_weight"]
        fc2_bias = archive["fc2_bias"]
        window, input_size, hidden_size = validate_model_arrays(fc1_weight, fc1_bias, fc2_weight, fc2_bias)
        return ControllerModel(
            window=window,
            input_size=input_size,
            hidden_size=hidden_size,
            scale=int(archive["scale"]),
            shift=int(archive["shift"]),
            fc1_weight=fc1_weight,
            fc1_bias=fc1_bias,
            fc2_weight=fc2_weight,
            fc2_bias=fc2_bias,
        )


def c_values(values: np.ndarray, values_per_line: int = 12) -> str:
    flat = values.reshape(-1)
    lines = []
    for start in range(0, len(flat), values_per_line):
        chunk = flat[start : start + values_per_line]
        lines.append("    " + ", ".join(str(int(value)) for value in chunk))
    return ",\n".join(lines)


def write_header(output_path: Path, model: ControllerModel) -> None:
    lines = [
        "#ifndef MIC32_MODEL_WEIGHTS_H",
        "#define MIC32_MODEL_WEIGHTS_H",
        "",
        "#include <stdint.h>",
        "",
        '#include "quant_model.h"',
        "",
        f"#define MODEL_WINDOW {model.window}u",
        f"#define MODEL_INPUT_SIZE {model.input_size}u",
        f"#define MODEL_HIDDEN_SIZE {model.hidden_size}u",
        f"#define MODEL_OUTPUT_SIZE {OUTPUT_SIZE}u",
        f"#define MODEL_SCALE {model.scale}",
        f"#define MODEL_SHIFT {model.shift}u",
        f"#define MODEL_RAW_BYTES {model.raw_bytes}u",
        "",
        "extern const int8_t MODEL_FC1_W[MODEL_HIDDEN_SIZE * MODEL_INPUT_SIZE];",
        "extern const int8_t MODEL_FC1_B[MODEL_HIDDEN_SIZE];",
        "extern const int8_t MODEL_FC2_W[MODEL_OUTPUT_SIZE * MODEL_HIDDEN_SIZE];",
        "extern const int8_t MODEL_FC2_B[MODEL_OUTPUT_SIZE];",
        "extern const QuantModel MODEL;",
        "",
        "#endif",
        "",
    ]
    output_path.write_text("\n".join(lines), encoding="utf-8")


def write_source(output_path: Path, model: ControllerModel) -> None:
    lines = [
        '#include "model_weights.h"',
        "",
        "const int8_t MODEL_FC1_W[MODEL_HIDDEN_SIZE * MODEL_INPUT_SIZE] = {",
        c_values(model.fc1_weight),
        "};",
        "",
        "const int8_t MODEL_FC1_B[MODEL_HIDDEN_SIZE] = {",
        c_values(model.fc1_bias),
        "};",
        "",
        "const int8_t MODEL_FC2_W[MODEL_OUTPUT_SIZE * MODEL_HIDDEN_SIZE] = {",
        c_values(model.fc2_weight),
        "};",
        "",
        "const int8_t MODEL_FC2_B[MODEL_OUTPUT_SIZE] = {",
        c_values(model.fc2_bias),
        "};",
        "",
        "const QuantModel MODEL = {",
        "    .window = MODEL_WINDOW,",
        "    .input_size = MODEL_INPUT_SIZE,",
        "    .hidden_size = MODEL_HIDDEN_SIZE,",
        "    .scale = MODEL_SCALE,",
        "    .shift = MODEL_SHIFT,",
        "    .fc1_w = MODEL_FC1_W,",
        "    .fc1_b = MODEL_FC1_B,",
        "    .fc2_w = MODEL_FC2_W,",
        "    .fc2_b = MODEL_FC2_B,",
        "};",
        "",
    ]
    output_path.write_text("\n".join(lines), encoding="utf-8")


def export_controller_weights(model_path: Path, output_dir: Path) -> None:
    model = load_one_hidden_model(model_path)
    output_dir.mkdir(parents=True, exist_ok=True)
    write_header(output_dir / "model_weights.h", model)
    write_source(output_dir / "model_weights.c", model)
    print(
        f"exported {model_path} -> {output_dir / 'model_weights.h'}, {output_dir / 'model_weights.c'} "
        f"raw_bytes={model.raw_bytes}"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Export one-hidden quantized model weights for C controller code.")
    parser.add_argument("--model-path", type=Path, default=Path("artifacts/sweep/work/w6_h16/model_quantized.npz"))
    parser.add_argument("--output-dir", type=Path, default=Path("controller"))
    args = parser.parse_args()
    export_controller_weights(args.model_path, args.output_dir)


if __name__ == "__main__":
    main()
