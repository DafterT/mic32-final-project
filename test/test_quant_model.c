#include "unity.h"

#include "quant_model.h"

void setUp(void) {
}

void tearDown(void) {
}

static QuantModel make_model(
    uint8_t window,
    uint16_t hidden_size,
    int32_t scale,
    uint8_t shift,
    const int8_t *fc1_w,
    const int8_t *fc1_b,
    const int8_t *fc2_w,
    const int8_t *fc2_b) {
    QuantModel model = {
        .window = window,
        .input_size = (uint16_t)(window * QUANT_MODEL_FEATURES_PER_ROUND),
        .hidden_size = hidden_size,
        .scale = scale,
        .shift = shift,
        .fc1_w = fc1_w,
        .fc1_b = fc1_b,
        .fc2_w = fc2_w,
        .fc2_b = fc2_b,
    };

    return model;
}

void test_quant_model_history_init_fills_window_and_resets_pending_move(void) {
    QuantModelHistory history = {
        .pending_move = 9u,
        .moves = {9u, 9u, 9u, 9u},
        .results = {8u, 8u, 8u, 8u},
    };
    QuantModel model = make_model(3u, 1u, 1, 0u, NULL, NULL, NULL, NULL);

    quant_model_history_init(&model, &history, 2u, 1u);

    TEST_ASSERT_EQUAL_UINT8(0u, history.pending_move);
    TEST_ASSERT_EQUAL_UINT8(2u, history.moves[0]);
    TEST_ASSERT_EQUAL_UINT8(2u, history.moves[1]);
    TEST_ASSERT_EQUAL_UINT8(2u, history.moves[2]);
    TEST_ASSERT_EQUAL_UINT8(1u, history.results[0]);
    TEST_ASSERT_EQUAL_UINT8(1u, history.results[1]);
    TEST_ASSERT_EQUAL_UINT8(1u, history.results[2]);
    // not must filling
    // TEST_ASSERT_EQUAL_UINT8(9u, history.moves[3]);
    // TEST_ASSERT_EQUAL_UINT8(8u, history.results[3]);
}

void test_quant_model_history_add_move_and_result_shift_history_to_end(void) {
    QuantModelHistory history = {
        .pending_move = 0u,
        .moves = {0u, 1u, 2u},
        .results = {10u, 11u, 12u},
    };
    QuantModel model = make_model(3u, 1u, 1, 0u, NULL, NULL, NULL, NULL);

    quant_model_history_add_move(&model, &history, 7u);

    TEST_ASSERT_EQUAL_UINT8(7u, history.pending_move);
    TEST_ASSERT_EQUAL_UINT8(0u, history.moves[0]);
    TEST_ASSERT_EQUAL_UINT8(1u, history.moves[1]);
    TEST_ASSERT_EQUAL_UINT8(2u, history.moves[2]);
    TEST_ASSERT_EQUAL_UINT8(10u, history.results[0]);
    TEST_ASSERT_EQUAL_UINT8(11u, history.results[1]);
    TEST_ASSERT_EQUAL_UINT8(12u, history.results[2]);

    quant_model_history_add_result(&model, &history, 8u);

    TEST_ASSERT_EQUAL_UINT8(0u, history.pending_move);
    TEST_ASSERT_EQUAL_UINT8(1u, history.moves[0]);
    TEST_ASSERT_EQUAL_UINT8(2u, history.moves[1]);
    TEST_ASSERT_EQUAL_UINT8(7u, history.moves[2]);
    TEST_ASSERT_EQUAL_UINT8(11u, history.results[0]);
    TEST_ASSERT_EQUAL_UINT8(12u, history.results[1]);
    TEST_ASSERT_EQUAL_UINT8(8u, history.results[2]);
}

void test_quant_model_logits_apply_scale_shift_relu_bias_and_negative_rounding(void) {
    const int8_t fc1_w[] = {
        1, 0, 0, 1, 0, 0,
        -1, 0, 0, 0, 0, 0,
        -1, 0, 0, -1, 0, 0,
    };
    const int8_t fc1_b[] = {-1, 3, 1};
    const int8_t fc2_w[] = {
        2, 0, 5,
        0, 3, 0,
        -1, 2, 0,
    };
    const int8_t fc2_b[] = {0, 0, 2};
    QuantModelHistory history = {
        .pending_move = 0u,
        .moves = {0u},
        .results = {0u},
    };
    QuantModel model = make_model(1u, 3u, 3, 1u, fc1_w, fc1_b, fc2_w, fc2_b);
    int32_t logits[QUANT_MODEL_OUTPUT_SIZE];

    quant_model_logits(&model, &history, logits);

    TEST_ASSERT_EQUAL_INT32(2, logits[0]);
    TEST_ASSERT_EQUAL_INT32(1, logits[1]);
    TEST_ASSERT_EQUAL_INT32(2, logits[2]);
}

void test_quant_model_predict_returns_max_logit_index(void) {
    const int8_t fc1_w[] = {0, 0, 0, 0, 0, 0};
    const int8_t fc1_b[] = {0};
    const int8_t fc2_w[] = {0, 0, 0};
    const int8_t fc2_b[] = {0, 5, -2};
    QuantModelHistory history = {0};
    QuantModel model = make_model(1u, 1u, 1, 0u, fc1_w, fc1_b, fc2_w, fc2_b);

    TEST_ASSERT_EQUAL_UINT8(1u, quant_model_predict(&model, &history));
}

void test_quant_model_predict_keeps_lower_index_on_equal_logits(void) {
    const int8_t fc1_w[] = {0, 0, 0, 0, 0, 0};
    const int8_t fc1_b[] = {0};
    const int8_t fc2_w[] = {0, 0, 0};
    const int8_t fc2_b[] = {0, 4, 4};
    QuantModelHistory history = {0};
    QuantModel model = make_model(1u, 1u, 1, 0u, fc1_w, fc1_b, fc2_w, fc2_b);

    TEST_ASSERT_EQUAL_UINT8(1u, quant_model_predict(&model, &history));
}
