
#ifndef __SRC_OPERATOR_TRUSTED_FUNCTIONALITY_H
#define __SRC_OPERATOR_TRUSTED_FUNCTIONALITY_H

#include "zcash.h"

int evaluate_functionality(
    uint8_t *outputs[AUNTIE_NUM_PLAYERS],
    size_t output_lengths[AUNTIE_NUM_PLAYERS],
    zatoshis_t payouts[AUNTIE_NUM_PLAYERS],
    uint8_t *const inputs[AUNTIE_NUM_PLAYERS],
    const size_t input_lengths[AUNTIE_NUM_PLAYERS],
    const zatoshis_t deposits[AUNTIE_NUM_PLAYERS]
);

#endif /* __SRC_OPERATOR_TRUSTED_FUNCTIONALITY_H */
