from __future__ import annotations

import argparse
import csv
import json
import math
import random
from pathlib import Path

import numpy as np
from tqdm import tqdm

from ml.game_rules import MOVE_NAMES, MOVES, one_hot, result_for_player
from ml.strategies import OPPONENT_STRATEGIES, STRATEGIES, History, RoundRecord

WINDOW = 5
FEATURES_PER_ROUND = 6
INPUT_SIZE = WINDOW * FEATURES_PER_ROUND


def input_size_for_window(window: int) -> int:
    return window * FEATURES_PER_ROUND


def csv_header(window: int) -> list[str]:
    return [f"x{i}" for i in range(input_size_for_window(window))] + ["target"]


def opponent_view(history: History) -> list[RoundRecord]:
    return [
        (opponent_move, player_move, result_for_player(opponent_move, player_move))
        for player_move, opponent_move, _result in history
    ]


def generate_game(
    player_strategy_name: str,
    opponent_strategy_name: str,
    rounds: int,
    rng: random.Random,
) -> list[RoundRecord]:
    player_strategy = STRATEGIES[player_strategy_name]
    opponent_strategy = OPPONENT_STRATEGIES[opponent_strategy_name]
    history: list[RoundRecord] = []

    for _ in range(rounds):
        player_move = player_strategy(history, rng)
        opponent_move = opponent_strategy(opponent_view(history), rng)
        result = result_for_player(player_move, opponent_move)
        history.append((player_move, opponent_move, result))

    return history


def encode_window(window: History) -> list[int]:
    features: list[int] = []
    for player_move, _opponent_move, result in window:
        features.extend(one_hot(player_move))
        features.extend(one_hot(result))
    return features


def windows_from_game(history: History, window: int = WINDOW) -> list[list[int]]:
    rows: list[list[int]] = []
    for index in range(window, len(history)):
        features = encode_window(history[index - window : index])
        rows.append(features + [history[index][0]])
    return rows


def balanced_strategy_plan(num_games: int, rng: random.Random) -> list[str]:
    names = list(STRATEGIES)
    repeats = math.ceil(num_games / len(names))
    plan = (names * repeats)[:num_games]
    rng.shuffle(plan)
    return plan


def generate_split_csv(
    output_path: Path,
    num_games: int,
    seed: int,
    min_rounds: int,
    max_rounds: int,
    window: int = WINDOW,
) -> dict[str, object]:
    rng = random.Random(seed)
    strategy_plan = balanced_strategy_plan(num_games, rng)
    opponent_names = list(OPPONENT_STRATEGIES)
    strategy_games = {name: 0 for name in STRATEGIES}
    target_counts = {MOVE_NAMES[move]: 0 for move in MOVES}
    sample_count = 0

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.writer(file)
        writer.writerow(csv_header(window))

        for player_name in tqdm(strategy_plan, desc=f"generate {output_path.name}"):
            opponent_name = rng.choice(opponent_names)
            rounds = rng.randint(min_rounds, max_rounds)
            history = generate_game(player_name, opponent_name, rounds, rng)
            rows = windows_from_game(history, window=window)
            writer.writerows(rows)

            strategy_games[player_name] += 1
            sample_count += len(rows)
            for row in rows:
                target_counts[MOVE_NAMES[row[-1]]] += 1

    return {
        "path": str(output_path),
        "seed": seed,
        "window": window,
        "games": num_games,
        "samples": sample_count,
        "strategy_games": strategy_games,
        "target_counts": target_counts,
    }


def generate_all_datasets(
    data_dir: Path,
    summary_path: Path,
    train_games: int = 340,
    val_games: int = 75,
    test_games: int = 75,
    min_rounds: int = 140,
    max_rounds: int = 240,
    seed: int = 42,
    window: int = WINDOW,
) -> dict[str, object]:
    splits = {
        "train": (train_games, seed),
        "val": (val_games, seed + 10_000),
        "test": (test_games, seed + 20_000),
    }
    summary = {
        split: generate_split_csv(
            data_dir / f"{split}.csv",
            num_games=games,
            seed=split_seed,
            min_rounds=min_rounds,
            max_rounds=max_rounds,
            window=window,
        )
        for split, (games, split_seed) in splits.items()
    }

    summary_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    return summary


def load_csv_numpy(path: Path) -> tuple[np.ndarray, np.ndarray]:
    data = np.loadtxt(path, delimiter=",", skiprows=1, dtype=np.float32)
    if data.ndim == 1:
        data = data.reshape(1, -1)
    features = data[:, :-1].astype(np.float32)
    targets = data[:, -1].astype(np.int64)
    return features, targets


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate synthetic datasets.")
    parser.add_argument("--data-dir", type=Path, default=Path("data"))
    parser.add_argument("--summary-path", type=Path, default=Path("artifacts/dataset_summary.json"))
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--train-games", type=int, default=340)
    parser.add_argument("--val-games", type=int, default=75)
    parser.add_argument("--test-games", type=int, default=75)
    parser.add_argument("--window", type=int, default=WINDOW)
    args = parser.parse_args()

    generate_all_datasets(
        data_dir=args.data_dir,
        summary_path=args.summary_path,
        train_games=args.train_games,
        val_games=args.val_games,
        test_games=args.test_games,
        seed=args.seed,
        window=args.window,
    )


if __name__ == "__main__":
    main()
