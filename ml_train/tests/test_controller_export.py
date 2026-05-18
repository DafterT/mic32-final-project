from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import numpy as np

try:
    from ._bootstrap import PROJECT_ROOT, add_project_root_to_path
except ImportError:
    from _bootstrap import PROJECT_ROOT, add_project_root_to_path

add_project_root_to_path()

from ml.dataset import FEATURES_PER_ROUND, encode_window
from ml.quantize import quantized_logits
from scripts.export_controller_weights import export_controller_weights

MODEL_PATH = PROJECT_ROOT / "artifacts" / "sweep" / "work" / "w6_h16" / "model_quantized.npz"
CONTROLLER_DIR = PROJECT_ROOT / "controller"
RUNNER_MOVES = [0, 1, 2, 0, 2, 1, 1, 0, 2, 2]
RUNNER_RESULTS = [1, 2, 0, 2, 1, 0, 2, 2, 1, 0]


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=True, capture_output=True, text=True)


def wsl_path(path: Path) -> str:
    text = str(path.resolve())
    if len(text) >= 2 and text[1] == ":":
        drive_path = text[2:].replace("\\", "/")
        return f"/mnt/{text[0].lower()}{drive_path}"
    return text.replace("\\", "/")


def c_array(rows: list[list[int]]) -> str:
    return ",\n".join("    {" + ", ".join(str(value) for value in row) + "}" for row in rows)


def build_samples(window: int) -> tuple[list[list[int]], list[list[int]]]:
    moves_rows: list[list[int]] = []
    results_rows: list[list[int]] = []
    for seed in range(12):
        moves_rows.append([(seed + index) % 3 for index in range(window)])
        results_rows.append([((seed // 3) + 2 * index) % 3 for index in range(window)])
    return moves_rows, results_rows


def features_for_history(moves: list[int], results: list[int]) -> np.ndarray:
    return np.array([encode_window([(move, 0, result) for move, result in zip(moves, results)])], dtype=np.float32)


def expected_runner_rows(model_path: Path, window: int) -> list[list[int]]:
    moves = [0] * window
    results = [1] * window
    rows: list[list[int]] = []
    for index, (move, result) in enumerate(zip(RUNNER_MOVES, RUNNER_RESULTS)):
        logits = quantized_logits(features_for_history(moves, results), model_path)[0].astype(int).tolist()
        prediction = int(np.argmax(logits))
        rows.append([index, move, result, *logits, prediction])
        moves = moves[1:] + [move]
        results = results[1:] + [result]
    return rows


def write_runner(path: Path, moves_rows: list[list[int]], results_rows: list[list[int]]) -> None:
    sample_count = len(moves_rows)
    path.write_text(
        "\n".join(
            [
                "#include <stdint.h>",
                "#include <stdio.h>",
                '#include "model_weights.h"',
                '#include "quant_model.h"',
                "",
                f"#define SAMPLE_COUNT {sample_count}u",
                "",
                "static const uint8_t MOVES[SAMPLE_COUNT][MODEL_WINDOW] = {",
                c_array(moves_rows),
                "};",
                "",
                "static const uint8_t RESULTS[SAMPLE_COUNT][MODEL_WINDOW] = {",
                c_array(results_rows),
                "};",
                "",
                "static void fill_history(",
                "    QuantModelHistory *history,",
                "    const uint8_t moves[MODEL_WINDOW],",
                "    const uint8_t results[MODEL_WINDOW]) {",
                "    uint8_t index;",
                "    quant_model_history_init(&MODEL, history, 0u, 1u);",
                "    for (index = 0; index < MODEL_WINDOW; ++index) {",
                "        quant_model_history_add_move(&MODEL, history, moves[index]);",
                "        quant_model_history_add_result(&MODEL, history, results[index]);",
                "    }",
                "}",
                "",
                "int main(void) {",
                "    uint8_t index;",
                "    for (index = 0; index < SAMPLE_COUNT; ++index) {",
                "        QuantModelHistory history;",
                "        int32_t logits[3];",
                "        uint8_t prediction;",
                "        fill_history(&history, MOVES[index], RESULTS[index]);",
                "        quant_model_logits(&MODEL, &history, logits);",
                "        prediction = quant_model_predict(&MODEL, &history);",
                "        printf(\"%u %ld %ld %ld %u\\n\",",
                "               (unsigned)index,",
                "               (long)logits[0],",
                "               (long)logits[1],",
                "               (long)logits[2],",
                "               (unsigned)prediction);",
                "    }",
                "    return 0;",
                "}",
                "",
            ]
        ),
        encoding="utf-8",
    )


def compile_and_run(runner_c: Path, weights_c: Path, include_dir: Path) -> str:
    local_gcc = shutil.which("gcc")
    if local_gcc is not None:
        exe_path = runner_c.with_suffix(".exe" if os.name == "nt" else "")
        run(
            [
                local_gcc,
                "-std=c99",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(include_dir),
                "-I",
                str(CONTROLLER_DIR),
                str(runner_c),
                str(CONTROLLER_DIR / "quant_model.c"),
                str(weights_c),
                "-o",
                str(exe_path),
            ]
        )
        return run([str(exe_path)]).stdout

    if shutil.which("wsl") is None:
        raise RuntimeError("Need local gcc or WSL gcc to compile controller test")

    run(["wsl", "sh", "-lc", "command -v gcc >/dev/null"])
    exe_wsl = f"/tmp/mic32_controller_test_{os.getpid()}"
    try:
        run(
            [
                "wsl",
                "gcc",
                "-std=c99",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                wsl_path(include_dir),
                "-I",
                wsl_path(CONTROLLER_DIR),
                wsl_path(runner_c),
                wsl_path(CONTROLLER_DIR / "quant_model.c"),
                wsl_path(weights_c),
                "-o",
                exe_wsl,
            ]
        )
        return run(["wsl", exe_wsl]).stdout
    finally:
        subprocess.run(["wsl", "rm", "-f", exe_wsl], check=False)


def parse_rows(output: str) -> list[list[int]]:
    return [[int(value) for value in line.split()] for line in output.splitlines() if line.strip()]


def compare_rows(name: str, actual_rows: list[list[int]], expected_rows: list[list[int]]) -> None:
    if len(actual_rows) != len(expected_rows):
        raise AssertionError(f"Expected {len(expected_rows)} {name} rows from C runner, got {len(actual_rows)}")

    for index, (actual_row, expected_row) in enumerate(zip(actual_rows, expected_rows)):
        if actual_row != expected_row:
            raise AssertionError(f"{name} row mismatch at {index}: {actual_row} != {expected_row}")


def build_feature_batch(moves_rows: list[list[int]], results_rows: list[list[int]]) -> np.ndarray:
    rows = [
        encode_window([(move, 0, result) for move, result in zip(moves, results)])
        for moves, results in zip(moves_rows, results_rows)
    ]
    return np.array(rows, dtype=np.float32)


def main() -> None:
    if not MODEL_PATH.exists():
        raise SystemExit(f"Missing {MODEL_PATH}. Run scripts/sweep_configs.py first.")

    with np.load(MODEL_PATH) as archive:
        window = archive["fc1_weight"].shape[1] // FEATURES_PER_ROUND
    moves_rows, results_rows = build_samples(window)
    features = build_feature_batch(moves_rows, results_rows)
    expected_logits = quantized_logits(features, MODEL_PATH).astype(int)
    expected_predictions = np.argmax(expected_logits, axis=1).astype(int)

    with tempfile.TemporaryDirectory() as temp_dir_name:
        temp_dir = Path(temp_dir_name)
        generated_dir = temp_dir / "generated"
        export_controller_weights(MODEL_PATH, generated_dir)
        for name in ("model_weights.h", "model_weights.c"):
            expected = (generated_dir / name).read_text(encoding="utf-8")
            actual = (CONTROLLER_DIR / name).read_text(encoding="utf-8")
            if expected != actual:
                raise AssertionError(f"controller/{name} is out of date; rerun scripts/export_controller_weights.py")

        runner_c = temp_dir / "controller_test_runner.c"
        write_runner(runner_c, moves_rows, results_rows)
        output = compile_and_run(runner_c, generated_dir / "model_weights.c", generated_dir)
        model_runner_output = compile_and_run(
            CONTROLLER_DIR / "model_runner.c",
            generated_dir / "model_weights.c",
            generated_dir,
        )

    actual_rows = parse_rows(output)
    if len(actual_rows) != len(expected_predictions):
        raise AssertionError(f"Expected {len(expected_predictions)} rows from C runner, got {len(actual_rows)}")
    for index, row in enumerate(actual_rows):
        actual_index, logit0, logit1, logit2, prediction = row
        expected_row = expected_logits[index].tolist()
        if actual_index != index:
            raise AssertionError(f"Row index mismatch: {actual_index} != {index}")
        if [logit0, logit1, logit2] != expected_row:
            raise AssertionError(f"Logits mismatch at {index}: {[logit0, logit1, logit2]} != {expected_row}")
        if prediction != int(expected_predictions[index]):
            raise AssertionError(f"Prediction mismatch at {index}: {prediction} != {int(expected_predictions[index])}")

    compare_rows("model_runner", parse_rows(model_runner_output), expected_runner_rows(MODEL_PATH, window))

    print(f"controller export OK: {len(actual_rows)} samples and model_runner matched Python through history API")


if __name__ == "__main__":
    main()
