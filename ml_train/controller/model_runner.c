#include <stdio.h>
#include <stdint.h>

#include "model_weights.h"
#include "quant_model.h"

#define RUNNER_ROUNDS 10u

static const uint8_t RUNNER_MOVES[RUNNER_ROUNDS] = {0u, 1u, 2u, 0u, 2u, 1u, 1u, 0u, 2u, 2u};
static const uint8_t RUNNER_RESULTS[RUNNER_ROUNDS] = {1u, 2u, 0u, 2u, 1u, 0u, 2u, 2u, 1u, 0u};

int main(void) {
    QuantModelHistory history;
    int32_t logits[3];
    uint8_t index;
    uint8_t move;
    uint8_t prediction;
    uint8_t result;

    quant_model_history_init(&MODEL, &history, 0u, 1u);

    for (index = 0; index < RUNNER_ROUNDS; ++index) {
        move = RUNNER_MOVES[index];
        result = RUNNER_RESULTS[index];
        quant_model_history_add_move(&MODEL, &history, move);
        quant_model_logits(&MODEL, &history, logits);
        prediction = quant_model_predict(&MODEL, &history);
        printf("%u %u %u %ld %ld %ld %u\n",
               (unsigned)index,
               (unsigned)move,
               (unsigned)result,
               (long)logits[0],
               (long)logits[1],
               (long)logits[2],
               (unsigned)prediction);

        quant_model_history_add_result(&MODEL, &history, result);
    }
    return 0;
}
