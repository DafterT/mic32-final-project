from __future__ import annotations

import random
from collections.abc import Callable, Sequence

from ml.game_rules import (
    MOVES,
    RESULT_DRAW,
    RESULT_LOSE,
    RESULT_WIN,
    WINNING_RESPONSE,
)

RoundRecord = tuple[int, int, int]
History = Sequence[RoundRecord]
Strategy = Callable[[History, random.Random], int]


def _weighted_move(rng: random.Random, weights: Sequence[float]) -> int:
    return rng.choices(MOVES, weights=weights, k=1)[0]


def _different_from(move: int, rng: random.Random) -> int:
    candidates = [candidate for candidate in MOVES if candidate != move]
    return rng.choice(candidates)


def random_player(history: History, rng: random.Random) -> int:
    return rng.choice(MOVES)


def repeat_player(history: History, rng: random.Random) -> int:
    if not history:
        return random_player(history, rng)
    return history[-1][0]


def no_repeat_player(history: History, rng: random.Random) -> int:
    if not history:
        return random_player(history, rng)
    return _different_from(history[-1][0], rng)


def cycle_forward_player(history: History, rng: random.Random) -> int:
    if not history:
        return rng.choice(MOVES)
    return (history[-1][0] + 1) % len(MOVES)


def cycle_backward_player(history: History, rng: random.Random) -> int:
    if not history:
        return rng.choice(MOVES)
    return (history[-1][0] - 1) % len(MOVES)


def rock_biased_player(history: History, rng: random.Random) -> int:
    return _weighted_move(rng, (0.70, 0.15, 0.15))


def paper_biased_player(history: History, rng: random.Random) -> int:
    return _weighted_move(rng, (0.15, 0.70, 0.15))


def scissors_biased_player(history: History, rng: random.Random) -> int:
    return _weighted_move(rng, (0.15, 0.15, 0.70))


def rock_avoid_player(history: History, rng: random.Random) -> int:
    return _weighted_move(rng, (0.10, 0.45, 0.45))


def paper_avoid_player(history: History, rng: random.Random) -> int:
    return _weighted_move(rng, (0.45, 0.10, 0.45))


def scissors_avoid_player(history: History, rng: random.Random) -> int:
    return _weighted_move(rng, (0.45, 0.45, 0.10))


def win_stay_player(history: History, rng: random.Random) -> int:
    if history and history[-1][2] == RESULT_WIN:
        return history[-1][0]
    return random_player(history, rng)


def lose_shift_player(history: History, rng: random.Random) -> int:
    if not history:
        return random_player(history, rng)
    if history[-1][2] == RESULT_LOSE:
        return _different_from(history[-1][0], rng)
    return history[-1][0]


def draw_shift_player(history: History, rng: random.Random) -> int:
    if not history:
        return random_player(history, rng)
    if history[-1][2] == RESULT_DRAW:
        return _different_from(history[-1][0], rng)
    return history[-1][0]


def anti_last_ai_player(history: History, rng: random.Random) -> int:
    if not history:
        return random_player(history, rng)
    return WINNING_RESPONSE[history[-1][1]]


def copy_last_ai_player(history: History, rng: random.Random) -> int:
    if not history:
        return random_player(history, rng)
    return history[-1][1]


def mixed_player(history: History, rng: random.Random) -> int:
    return rng.choice(MIXED_STRATEGIES)(history, rng)


MIXED_STRATEGIES: tuple[Strategy, ...] = (
    repeat_player,
    no_repeat_player,
    cycle_forward_player,
    cycle_backward_player,
    win_stay_player,
    lose_shift_player,
    draw_shift_player,
    anti_last_ai_player,
    copy_last_ai_player,
)

PLAYER_STRATEGIES: tuple[Strategy, ...] = (
    random_player,
    repeat_player,
    no_repeat_player,
    cycle_forward_player,
    cycle_backward_player,
    rock_biased_player,
    paper_biased_player,
    scissors_biased_player,
    rock_avoid_player,
    paper_avoid_player,
    scissors_avoid_player,
    win_stay_player,
    lose_shift_player,
    draw_shift_player,
    anti_last_ai_player,
    copy_last_ai_player,
    mixed_player,
)

OPPONENT_STRATEGY_NAMES = (
    "random_player",
    "repeat_player",
    "no_repeat_player",
    "cycle_forward_player",
    "cycle_backward_player",
    "rock_biased_player",
    "paper_biased_player",
    "scissors_biased_player",
)

STRATEGIES: dict[str, Strategy] = {strategy.__name__: strategy for strategy in PLAYER_STRATEGIES}
OPPONENT_STRATEGIES: dict[str, Strategy] = {name: STRATEGIES[name] for name in OPPONENT_STRATEGY_NAMES}
