#include "setup.h"
#include "core.h"
#include "printf.h"
#include <errno.h>

enum state {
    UNINITIALIZED = 0,
    INITIALIZED,
    DEPOSITED,
    GOT_DEPOSITS,
    CONFIRMED_DEPOSITS,
    SETTLED,
    REFUNDED,
    ABORTED
};
static enum state state;
#define CHECK_STATE(expected_state) { \
    if (state != expected_state) { \
        printf("%s: TEE in bad state %u (expected %u)\n", __func__, state, expected_state); \
        return -EINVAL; \
    } \
}
#define CHECK_STATE_NOT(unexpected_state) { \
    if (state == unexpected_state) { \
        printf("%s: TEE in bad state %u\n", __func__, state); \
        return -EINVAL; \
    } \
}

int ecall_connect_to_operator(void *context, uint32_t player_id)
{
    return ecall_connect_to_operator_impl(context, player_id);
}

int ecall_initialize(void *context)
{
    int ret;
    CHECK_STATE(UNINITIALIZED);
    ret = ecall_initialize_impl(context);
    if (!ret)
        state = INITIALIZED;
    return ret;
}

int ecall_deposit_and_input(const uint8_t *deposit_transaction, size_t deposit_transaction_length, const uint8_t *input, size_t input_length)
{
    int ret;
    CHECK_STATE(INITIALIZED);
    ret = ecall_deposit_and_input_impl(deposit_transaction, deposit_transaction_length, input, input_length);
    /* Errors at this point abort the contract */
    if (ret)
        state = ABORTED;
    else
        state = DEPOSITED;
    return ret;
}

int ecall_get_deposits(void *context)
{
    int ret;
    CHECK_STATE(DEPOSITED);
    ret = ecall_get_deposits_impl(context);
    /* Errors at this point abort the contract */
    if (ret)
        state = ABORTED;
    else
        state = GOT_DEPOSITS;
    return ret;
}

int ecall_confirm_deposits(void)
{
    int ret;
    CHECK_STATE(GOT_DEPOSITS);
    ret = ecall_confirm_deposits_impl();
    /* Errors at this point abort the contract */
    if (ret)
        state = ABORTED;
    else
        state = CONFIRMED_DEPOSITS;
    return ret;
}

int ecall_settle(void *context, const uint8_t *blocks, size_t blocks_length)
{
    int ret;
    CHECK_STATE(CONFIRMED_DEPOSITS);
    ret = ecall_settle_impl(context, blocks, blocks_length);
    if (!ret)
        state = SETTLED;
    return ret;
}

int ecall_refund(void *context, const uint8_t *blocks, size_t blocks_length)
{
    int ret;
    CHECK_STATE_NOT(SETTLED);
    ret = ecall_refund_impl(context, blocks, blocks_length);
    if (!ret)
        state = REFUNDED;
    return ret;
}
