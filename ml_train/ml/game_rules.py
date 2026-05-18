from __future__ import annotations

MOVE_ROCK = 0
MOVE_PAPER = 1
MOVE_SCISSORS = 2

RESULT_LOSE = 0
RESULT_DRAW = 1
RESULT_WIN = 2

MOVES = (MOVE_ROCK, MOVE_PAPER, MOVE_SCISSORS)
MOVE_NAMES = {
    MOVE_ROCK: "rock",
    MOVE_PAPER: "paper",
    MOVE_SCISSORS: "scissors",
}
RESULT_NAMES = {
    RESULT_LOSE: "lose",
    RESULT_DRAW: "draw",
    RESULT_WIN: "win",
}

BEATS = {
    MOVE_ROCK: MOVE_SCISSORS,
    MOVE_PAPER: MOVE_ROCK,
    MOVE_SCISSORS: MOVE_PAPER,
}

WINNING_RESPONSE = {
    MOVE_ROCK: MOVE_PAPER,
    MOVE_PAPER: MOVE_SCISSORS,
    MOVE_SCISSORS: MOVE_ROCK,
}


def result_for_player(player_move: int, opponent_move: int) -> int:
    if player_move == opponent_move:
        return RESULT_DRAW
    if BEATS[player_move] == opponent_move:
        return RESULT_WIN
    return RESULT_LOSE


def counter_move(predicted_player_move: int) -> int:
    return WINNING_RESPONSE[predicted_player_move]


def one_hot(value: int, size: int = 3) -> list[int]:
    encoded = [0] * size
    encoded[value] = 1
    return encoded
