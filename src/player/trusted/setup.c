#include "setup.h"
#include "mutual_attestation.h"
#include "ocall.h"
#include "messages.h"
#include "mutual_attestation_common.h"
#include <errno.h>
#include <string.h>

struct operator *operator;

static inline int present_quotes(uint32_t player_id)
{
    struct auntie_msg *msg1, *msg2, *msg3;
    struct auntie_msg_players_quotes_broadcast *payload;
    uint32_t end;
    uint32_t length;
    int ret;

    printf("%s: waiting for the operator to send everybody's quotes\n", __func__);

    /* Wait for the operator to send us all quotes */
    msg1 = auntie_msg_receive(operator->channel);
    if (!msg1) {
        printf("%s: failed to receive the quotes of the counterparties\n", __func__);
        return -EIO;
    }
    if (auntie_msg_get_type(msg1) != AUNTIE_MSG_PLAYERS_QUOTES_BROADCAST) {
        printf("%s: unexpected message %u received (expected: %u)\n", \
            __func__, auntie_msg_get_type(msg1), AUNTIE_MSG_PLAYERS_QUOTES_BROADCAST);
        auntie_msg_destroy(msg1);
        return -EIO;
    }

    printf("%s: outputting the quotes for verification\n", __func__);
    payload = auntie_msg_get_payload(msg1);
    /* Present each quote to our host */
    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++) {
        /* Skip our own quote */
        if (i == player_id - 1)
            continue;

        end = (i < AUNTIE_NUM_PLAYERS - 1) ? payload->quotes_offsets[i + 1] : auntie_msg_get_payload_size(msg1) - sizeof(*payload);
        length = end - payload->quotes_offsets[i];

        ret = OCALL(ocall_print_quote, payload->quotes + payload->quotes_offsets[i], length, payload->report_data[i]);
        if (ret) {
            printf("%s: failed to print quote for player %d with error %d\n", __func__, i + 1, ret);
            auntie_msg_destroy(msg1);
            return -EIO;
        }
    }
    auntie_msg_destroy(msg1);

    /* Print quote of the operator */
    ret = OCALL(ocall_print_quote, operator->quote, operator->quote_length, operator->report_data);
    if (ret) {
        printf("%s: failed to print quote of the operator with error %d\n", __func__, ret);
        return -EIO;
    }

    printf("%s: sending an acknowledgement\n", __func__);
    /* Send an acknowledgement */
    msg2 = auntie_msg_create(AUNTIE_MSG_PLAYERS_QUOTES_OK, 0);
    if (!msg2) {
        printf("%s: failed to create players' quotes acknowledgement\n", __func__);
        return -ENOMEM;
    }

    ret = auntie_msg_send(msg2, operator->channel);
    auntie_msg_destroy(msg2);
    if (ret) {
        printf("%s: failed to send players' quotes acknowledgement with error %d\n", __func__, ret);
        return ret;
    }

    printf("%s: waiting for notification that contract is live\n", __func__);
    /* Receive notification that other parties confirmed as well and the contract is live */
    msg3 = auntie_msg_receive(operator->channel);
    if (!msg3) {
        printf("%s: failed to receive the setup complete notification\n", __func__);
        return -EIO;
    }
    if (auntie_msg_get_type(msg3) != AUNTIE_MSG_SETUP_COMPLETE) {
        printf("%s: unexpected message %u received (expected: %u)\n", \
            __func__, auntie_msg_get_type(msg3), AUNTIE_MSG_SETUP_COMPLETE);
        auntie_msg_destroy(msg3);
        return -EIO;
    }
    auntie_msg_destroy(msg3);

    printf("%s: all parties received each other's quotes and the contract is live\n", __func__);
    return 0;
}

int ecall_connect_to_operator_impl(void *context, uint32_t player_id)
{
    sgx_target_info_t quoting_enclave;
    int ret;

    if (operator) {
        printf("%s: already connected to the operator\n", __func__);
        return -EBUSY;
    }

    ret = OCALL(ocall_get_quoting_enclave, &quoting_enclave);
    if (ret) {
        printf("%s: failed to get quoting enclave\n", __func__);
        return ret;
    }

    operator = mutual_attestation(context, &quoting_enclave, player_id);
    if (!operator) {
        printf("%s: failed to establish a secure channel\n", __func__);
        return -EIO;
    }

    ret = present_quotes(player_id);
    if (ret) {
        printf("%s: failed to present counterparties' TEEs' quotes to the host with error %d\n", __func__, ret);
        channel_teardown(operator->channel);
        free(operator->quote);
        free(operator);
        operator = NULL;
        return ret;
    }

    return 0;
}
