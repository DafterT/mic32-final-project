#ifndef MIC32_MODEL_WEIGHTS_H
#define MIC32_MODEL_WEIGHTS_H

#include <stdint.h>

#include "quant_model.h"

#define MODEL_WINDOW 6u
#define MODEL_INPUT_SIZE 36u
#define MODEL_HIDDEN_SIZE 16u
#define MODEL_OUTPUT_SIZE 3u
#define MODEL_SCALE 64
#define MODEL_SHIFT 6u
#define MODEL_RAW_BYTES 643u

extern const int8_t MODEL_FC1_W[MODEL_HIDDEN_SIZE * MODEL_INPUT_SIZE];
extern const int8_t MODEL_FC1_B[MODEL_HIDDEN_SIZE];
extern const int8_t MODEL_FC2_W[MODEL_OUTPUT_SIZE * MODEL_HIDDEN_SIZE];
extern const int8_t MODEL_FC2_B[MODEL_OUTPUT_SIZE];
extern const QuantModel MODEL;

#endif
