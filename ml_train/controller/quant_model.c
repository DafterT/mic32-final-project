#include "quant_model.h"

static int32_t quant_shift(int32_t value, uint8_t shift) {
    int32_t divisor;

    if (shift == 0u) {
        return value;
    }

    divisor = (int32_t)1 << shift;
    if (value >= 0) {
        return value / divisor;
    }
    return -(((-value) + divisor - 1) / divisor);
}

static int32_t relu(int32_t value) {
    return value > 0 ? value : 0;
}

void quant_model_logits(const QuantModel *model, const QuantModelHistory *history, int32_t logits[3]) {
    int32_t hidden[QUANT_MODEL_MAX_HIDDEN_SIZE];
    uint16_t h;
    uint16_t o;

    logits[0] = 0;
    logits[1] = 0;
    logits[2] = 0;

    for (h = 0; h < model->hidden_size; ++h) {
        int32_t sum = 0;
        uint8_t round;

        for (round = 0; round < model->window; ++round) {
            uint16_t base = (uint16_t)(round * QUANT_MODEL_FEATURES_PER_ROUND);
            uint8_t move = history->moves[round];
            uint8_t result = history->results[round];
            const int8_t *weights = &model->fc1_w[(uint32_t)h * model->input_size];
            sum += model->scale * (int32_t)weights[base + move];
            sum += model->scale * (int32_t)weights[base + 3u + result];
        }

        hidden[h] = relu(quant_shift(sum, model->shift) + (int32_t)model->fc1_b[h]);
    }

    for (o = 0; o < QUANT_MODEL_OUTPUT_SIZE; ++o) {
        int32_t sum = 0;
        const int8_t *weights = &model->fc2_w[(uint32_t)o * model->hidden_size];

        for (h = 0; h < model->hidden_size; ++h) {
            sum += hidden[h] * (int32_t)weights[h];
        }

        logits[o] = quant_shift(sum, model->shift) + (int32_t)model->fc2_b[o];
    }
}

uint8_t quant_model_predict(const QuantModel *model, const QuantModelHistory *history) {
    int32_t logits[3];
    uint8_t best = 0;
    uint8_t index;

    quant_model_logits(model, history, logits);
    for (index = 1; index < QUANT_MODEL_OUTPUT_SIZE; ++index) {
        if (logits[index] > logits[best]) {
            best = index;
        }
    }
    return best;
}

void quant_model_history_init(
    const QuantModel *model,
    QuantModelHistory *history,
    uint8_t fill_move,
    uint8_t fill_result) {
    uint8_t index;

    history->pending_move = 0u;
    for (index = 0; index < model->window; ++index) {
        history->moves[index] = fill_move;
        history->results[index] = fill_result;
    }
}

void quant_model_history_add_move(const QuantModel *model, QuantModelHistory *history, uint8_t move) {
    (void)model;

    history->pending_move = move;
}

void quant_model_history_add_result(const QuantModel *model, QuantModelHistory *history, uint8_t result) {
    uint8_t index;

    for (index = 1u; index < model->window; ++index) {
        history->moves[index - 1u] = history->moves[index];
        history->results[index - 1u] = history->results[index];
    }
    history->moves[model->window - 1u] = history->pending_move;
    history->results[model->window - 1u] = result;
    history->pending_move = 0u;
}
