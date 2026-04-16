#include "mutual_attestation.h"
#include "functionality.h"
#include "ocall.h"
#include "core.h"
#include <sgx_report.h>
#include <stddef.h>
#include <errno.h>

static sgx_target_info_t quoting_enclave;
static int quoting_enclave_known;

enum state {
    UNINITIALIZED = 0,
    INITIALIZED,
    CLEARED_CONTRACT,
    FINALIZED,
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

int ecall_connect_to_player(void *context)
{
    int ret;
    struct player *player;

    if (all_players_connected()) {
        printf("%s: all players already connected\n", __func__);
        return -EBUSY;
    }

    /* Only query once about the quoting enclave */
    if (!quoting_enclave_known) {
        ret = OCALL(ocall_get_quoting_enclave, &quoting_enclave);
        if (ret) {
            printf("%s: failed to get quoting enclave\n", __func__);
            return ret;
        }
        quoting_enclave_known = 1;
    }

    player = mutual_attestation(context, &quoting_enclave);
    if (!player) {
        printf("%s: failed to establish a secure channel\n", __func__);
        return -EIO;
    }
    add_player(player);

    /* If all playeres have connected, present the quotes of all
     * of them to both our host and the players */
    if (all_players_connected())
        return present_quotes();

    return 0;
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

int ecall_clear_contract(void *context, const uint8_t *payout_address, size_t payout_address_length, const uint8_t *collateral_transaction, size_t collateral_transaction_length)
{
    int ret;
    CHECK_STATE(INITIALIZED);
    ret = ecall_clear_contract_impl(context, payout_address, payout_address_length, collateral_transaction, collateral_transaction_length);
    /* Errors at this point abort the contract */
    if (ret)
        state = ABORTED;
    else
        state = CLEARED_CONTRACT;
    return ret;
}

int ecall_finalize(void *context)
{
    int ret;
    CHECK_STATE(CLEARED_CONTRACT);
    ret = ecall_finalize_impl(context);
    /* Errors at this point abort the contract */
    if (ret)
        state = ABORTED;
    else
        state = FINALIZED;
    return ret;
}

int ecall_refund(void *context, const uint8_t *blocks, size_t blocks_length)
{
    int ret;
    CHECK_STATE_NOT(FINALIZED);
    ret = ecall_refund_impl(context, blocks, blocks_length);
    if (!ret)
        state = REFUNDED;
    return ret;
}
