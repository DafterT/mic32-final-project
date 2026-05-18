from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np

try:
    from ._bootstrap import add_project_root_to_path
except ImportError:
    from _bootstrap import add_project_root_to_path

add_project_root_to_path()

from ml.dataset import FEATURES_PER_ROUND, encode_window
from ml.game_rules import (
    MOVE_NAMES,
    MOVES,
    RESULT_DRAW,
    RESULT_LOSE,
    RESULT_NAMES,
    RESULT_WIN,
    counter_move,
    result_for_player,
)
from ml.quantize import quantized_logits
from ml.strategies import RoundRecord

GameHistory = list[RoundRecord]

ALIASES = {
    "0": 0,
    "r": 0,
    "rock": 0,
    "1": 1,
    "p": 1,
    "paper": 1,
    "2": 2,
    "s": 2,
    "scissors": 2,
}


def parse_move(text: str) -> int | None:
    return ALIASES.get(text.strip().lower())


def prompt_move() -> int | None:
    prompt = "move [0/r/rock, 1/p/paper, 2/s/scissors, q=quit]: "
    while True:
        value = input(prompt).strip().lower()
        if value in {"q", "quit", "exit"}:
            return None
        move = parse_move(value)
        if move is not None:
            return move
        print("Unknown move. Use 0/1/2 or rock/paper/scissors.")


def deterministic_warmup_move(round_index: int) -> int:
    return MOVES[round_index % len(MOVES)]


def window_from_model(model_path: Path) -> int:
    with np.load(model_path) as archive:
        input_size = int(archive["fc1_weight"].shape[1])
    if input_size % FEATURES_PER_ROUND != 0:
        raise ValueError(f"Model input size {input_size} is not divisible by {FEATURES_PER_ROUND}")
    return input_size // FEATURES_PER_ROUND


def predict_player_move(history: GameHistory, model_path: Path, window: int) -> tuple[int, list[int]]:
    features = np.array([encode_window(history[-window:])], dtype=np.float32)
    logits = quantized_logits(features, model_path)[0]
    return int(np.argmax(logits)), logits.astype(int).tolist()


def choose_ai_move(
    history: GameHistory,
    model_path: Path,
    window: int,
) -> tuple[int, int | None, list[int] | None, str]:
    if len(history) < window:
        ai_move = deterministic_warmup_move(len(history))
        return ai_move, None, None, "warmup"

    predicted_move, logits = predict_player_move(history, model_path, window)
    return counter_move(predicted_move), predicted_move, logits, "model"


def print_round(
    round_number: int,
    player_move: int,
    ai_move: int,
    result: int,
    predicted_move: int | None,
    logits: list[int] | None,
    mode: str,
    window: int,
) -> None:
    if predicted_move is None:
        prediction = f"{mode}: need {max(window - round_number, 0)} more history rounds"
    else:
        prediction = f"predicted={MOVE_NAMES[predicted_move]} logits={logits}"

    print(
        f"round={round_number} {prediction} | "
        f"you={MOVE_NAMES[player_move]} ai={MOVE_NAMES[ai_move]} result={RESULT_NAMES[result]}"
    )


def score_counts(history: GameHistory) -> tuple[int, int, int]:
    wins = sum(1 for _player, _ai, result in history if result == RESULT_WIN)
    draws = sum(1 for _player, _ai, result in history if result == RESULT_DRAW)
    losses = sum(1 for _player, _ai, result in history if result == RESULT_LOSE)
    return wins, draws, losses


def print_score(history: GameHistory) -> None:
    wins, draws, losses = score_counts(history)
    total = len(history)
    ai_wins = losses
    print(f"score: rounds={total} you_win={wins} draw={draws} ai_win={ai_wins}")


def play(model_path: Path, max_rounds: int) -> None:
    if not model_path.exists():
        raise SystemExit(f"Quantized model not found: {model_path}. Run scripts/run_pipeline.py first.")

    window = window_from_model(model_path)
    history: GameHistory = []
    print("Quantized model console game")
    print(f"model: {model_path}")
    print(f"window: {window}")
    print(f"first {window} rounds: deterministic warmup, then quantized model controls AI")

    while max_rounds <= 0 or len(history) < max_rounds:
        player_move = prompt_move()
        if player_move is None:
            break

        ai_move, predicted_move, logits, mode = choose_ai_move(history, model_path, window)
        result = result_for_player(player_move, ai_move)
        history.append((player_move, ai_move, result))
        print_round(len(history), player_move, ai_move, result, predicted_move, logits, mode, window)
        print_score(history)

    print("game finished")
    print_score(history)


def main() -> None:
    parser = argparse.ArgumentParser(description="Play against quantized int8 model in console.")
    parser.add_argument(
        "--model-path",
        type=Path,
        default=Path("artifacts/sweep/work/w6_h16/model_quantized.npz"),
        help="Path to model_quantized.npz.",
    )
    parser.add_argument("--rounds", type=int, default=0, help="Max rounds, 0 means no limit.")
    args = parser.parse_args()
    play(args.model_path, args.rounds)


if __name__ == "__main__":
    main()
