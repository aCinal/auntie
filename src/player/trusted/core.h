
#ifndef __SRC_PLAYER_TRUSTED_CORE_H
#define __SRC_PLAYER_TRUSTED_CORE_H

#include <stdint.h>
#include <stddef.h>

int ecall_initialize_impl(void *context);
int ecall_deposit_and_input_impl(const uint8_t *deposit_transaction, size_t deposit_transaction_length, const uint8_t *input, size_t input_length);
int ecall_get_deposits_impl(void *context);
int ecall_confirm_deposits_impl(void);
int ecall_settle_impl(void *context, const uint8_t *blocks, size_t blocks_length);
int ecall_refund_impl(void *context, const uint8_t *blocks, size_t blocks_length);

#endif /* __SRC_PLAYER_TRUSTED_CORE_H */
