
#ifndef __SRC_PLAYER_TRUSTED_SETUP_H
#define __SRC_PLAYER_TRUSTED_SETUP_H

#include "mutual_attestation.h"
#include <stdint.h>

extern struct operator *operator;

int ecall_connect_to_operator_impl(void *context, uint32_t player_id);

#endif /* __SRC_PLAYER_TRUSTED_SETUP_H */
