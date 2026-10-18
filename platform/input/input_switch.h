#ifndef RSL_INPUT_SWITCH_H
#define RSL_INPUT_SWITCH_H

#include <switch.h>

#include "input.h"

void rsl_switch_input_init(PadState *pad);
void rsl_switch_input_read(PadState *pad, rsl_raw_input *out);
uint32_t rsl_switch_pad_signature(const PadState *pad);

#endif
