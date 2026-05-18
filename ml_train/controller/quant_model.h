#ifndef MIC32_QUANT_MODEL_H
#define MIC32_QUANT_MODEL_H

#include <stdint.h>

#define QUANT_MODEL_FEATURES_PER_ROUND 6u
#define QUANT_MODEL_OUTPUT_SIZE 3u
#define QUANT_MODEL_MAX_HIDDEN_SIZE 32u
#define QUANT_MODEL_MAX_WINDOW 8u

typedef struct {
    uint8_t window;
    uint16_t input_size;
    uint16_t hidden_size;
    int32_t scale;
    uint8_t shift;
    const int8_t *fc1_w;
    const int8_t *fc1_b;
    const int8_t *fc2_w;
    const int8_t *fc2_b;
} QuantModel;

typedef struct {
    uint8_t pending_move;
    uint8_t moves[QUANT_MODEL_MAX_WINDOW];
    uint8_t results[QUANT_MODEL_MAX_WINDOW];
} QuantModelHistory;

void quant_model_logits(const QuantModel *model, const QuantModelHistory *history, int32_t logits[3]);
uint8_t quant_model_predict(const QuantModel *model, const QuantModelHistory *history);
void quant_model_history_init(
    const QuantModel *model,
    QuantModelHistory *history,
    uint8_t fill_move,
    uint8_t fill_result);
void quant_model_history_add_move(const QuantModel *model, QuantModelHistory *history, uint8_t move);
void quant_model_history_add_result(const QuantModel *model, QuantModelHistory *history, uint8_t result);

#endif
