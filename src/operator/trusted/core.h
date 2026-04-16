
#ifndef __SRC_OPERATOR_TRUSTED_CORE_H
#define __SRC_OPERATOR_TRUSTED_CORE_H

#include <stdint.h>
#include <stddef.h>

int ecall_initialize_impl(void *context);
int ecall_clear_contract_impl(void *context, const uint8_t *payout_address, size_t payout_address_length, const uint8_t *collateral_transaction, size_t collateral_transaction_length);
int ecall_finalize_impl(void *context);
int ecall_refund_impl(void *context, const uint8_t *blocks, size_t blocks_length);

#endif /* __SRC_OPERATOR_TRUSTED_CORE_H */
