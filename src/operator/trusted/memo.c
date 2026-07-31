#include "printf.h"
#include <sgx_utils.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

void get_settlement_memo(uint8_t memo[512])
{
    /* Include a common memo in the settlement transaction identifying the execution of the protocol
     * to parties with a viewing key of one of the recipients */
    sgx_target_info_t target_info;
    sgx_status_t status;
    char *cursor;
    size_t space_remaining;
    int written;

    (void) memset(memo, 0, 512);

    status = sgx_self_target(&target_info);
    if (status != SGX_SUCCESS) {
        printf("%s: sgx_self_target failed with error 0x%x", __func__, status);
        return;
    }

    space_remaining = 512;
    cursor = (char *) memo;

#define APPEND_TO_MEMO(__fmt, ...) ({ \
        written = snprintf(cursor, space_remaining, __fmt, ##__VA_ARGS__); \
        space_remaining -= written; \
        cursor += written; \
    })

    APPEND_TO_MEMO("Auntie protocol (https://eprint.iacr.org/2025/1965), ");
    APPEND_TO_MEMO("number of players: %d, ", AUNTIE_NUM_PLAYERS);
    APPEND_TO_MEMO("operator collateral: %d, ", AUNTIE_OPERATOR_COLLATERAL);
    APPEND_TO_MEMO("operator fee per player: %d, ", AUNTIE_OPERATOR_FEE_PER_PLAYER);
    APPEND_TO_MEMO("settle delay: %d, ", AUNTIE_SETTLE_DELAY_BLOCKS);
    APPEND_TO_MEMO("refund delay: %d, ", AUNTIE_REFUND_DELAY_BLOCKS);
    APPEND_TO_MEMO("operator's MRENCLAVE: ");
    for (int i = 0; i < sizeof(target_info.mr_enclave.m); i++)
        APPEND_TO_MEMO("%02x", target_info.mr_enclave.m[i]);
    APPEND_TO_MEMO(", attributes.flags=%" PRIx64 ", ", target_info.attributes.flags);
    APPEND_TO_MEMO("attributes.xfrm=%" PRIx64, target_info.attributes.xfrm);
}
