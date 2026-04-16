#include "mutual_attestation.h"
#include "mutual_attestation_common.h"
#include "messages.h"
#include "channel.h"
#include "ocall.h"
#include <sgx_utils.h>
#include <string.h>
#include <errno.h>

static uint8_t *stashed_quote;
static uint32_t stashed_quote_size;

static inline int stash_quote(const uint8_t *quote, uint32_t size)
{
    stashed_quote = malloc(size);
    if (!stashed_quote)
        return -ENOMEM;
    (void) memcpy(stashed_quote, quote, size);
    stashed_quote_size = size;
    return 0;
}

static inline void drop_stashed_quote(void)
{
    if (stashed_quote)
        free(stashed_quote);
    stashed_quote = NULL;
    stashed_quote_size = 0;
}

int ecall_quote_me_callback(const uint8_t *quote, uint32_t size)
{
    return stash_quote(quote, size);
}

struct operator *mutual_attestation(void *context, sgx_target_info_t *quoting_enclave, uint32_t id)
{
    sgx_report_t report;
    sgx_report_data_t player_report_data;
    sgx_report_data_t operator_report_data;
    sgx_ecc_state_handle_t ecc;
    sgx_ec256_private_t a;
    sgx_ec256_public_t ga;
    sgx_ec256_dh_shared_t gab;
    sgx_status_t status;
    sgx_key_128bit_t session_key;
    struct auntie_msg *msg1, *msg2, *msg3;
    struct auntie_msg_attestation_syn *syn;
    struct auntie_msg_attestation_synack *synack;
    struct auntie_msg_attestation_ack *ack;
    struct channel *channel;
    uint8_t nonce[ATTESTATION_NONCE_SIZE];
    uint8_t *operator_quote;
    uint32_t operator_quote_size;
    struct operator *operator;
    int ret;

    /* Choose a nonce for the operator's attestation */
    status = sgx_read_rand(nonce, sizeof(nonce));
    if (status != SGX_SUCCESS) {
        printf("%s: sgx_read_rand failed with error 0x%x\n", __func__, status);
        return NULL;
    }

    /* Open a plaintext channel */
    channel = channel_create(context);
    if (!channel) {
        printf("%s: failed to create a channel to the operator\n", __func__);
        return NULL;
    }
    channel_assign_id(channel, 0);

    /* Create our own secp256r1 Diffie-Hellman share */
    status = sgx_ecc256_open_context(&ecc);
    if (SGX_SUCCESS != status) {
        printf("%s: sgx_ecc256_open_context failed with error 0x%x\n", \
            __func__, status);
        channel_teardown(channel);
        return NULL;
    }

    status = sgx_ecc256_create_key_pair(&a, &ga, ecc);
    if (SGX_SUCCESS != status) {
        printf("%s: sgx_ecc256_create_key_pair failed with error 0x%x\n", \
            __func__, status);
        sgx_ecc256_close_context(ecc);
        channel_teardown(channel);
        return NULL;
    }

    printf("%s: establishing a channel with the operator, sending the SYN message\n", __func__);
    /* Send the SYN message */
    msg1 = auntie_msg_create(AUNTIE_MSG_ATTESTATION_SYN, sizeof(struct auntie_msg_attestation_syn));
    if (!msg1) {
        printf("%s: failed to create the SYN message\n", __func__);
        sgx_ecc256_close_context(ecc);
        channel_teardown(channel);
    }
    syn = auntie_msg_get_payload(msg1);
    syn->ga = ga;
    syn->id = id;
    (void) memcpy(syn->nonce, nonce, sizeof(syn->nonce));

    ret = auntie_msg_send(msg1, channel);
    auntie_msg_destroy(msg1);
    if (ret) {
        printf("%s: failed to send the SYN message with error %d\n", __func__, ret);
        sgx_ecc256_close_context(ecc);
        channel_teardown(channel);
        return NULL;
    }

    printf("%s: waiting for the SYN/ACK message\n", __func__);
    /* Receive the SYN/ACK message (Gb, nonce', quote) */
    msg2 = auntie_msg_receive(channel);
    if (!msg2) {
        printf("%s: failed to receive the SYN/ACK message\n", __func__);
        sgx_ecc256_close_context(ecc);
        channel_teardown(channel);
        return NULL;
    }

    if (auntie_msg_get_type(msg2) != AUNTIE_MSG_ATTESTATION_SYNACK) {
        printf("%s: received unexpected message %u (expected: %u)\n", \
            __func__, auntie_msg_get_type(msg2), AUNTIE_MSG_ATTESTATION_SYNACK);
        sgx_ecc256_close_context(ecc);
        channel_teardown(channel);
        auntie_msg_destroy(msg2);
        return NULL;
    }

    if (auntie_msg_get_payload_size(msg2) < sizeof(struct auntie_msg_attestation_synack)) {
        printf("%s: unexpected payload size %u for the SYN/ACK message (expected at least %u)\n", \
            __func__, auntie_msg_get_payload_size(msg2), sizeof(struct auntie_msg_attestation_synack));
        sgx_ecc256_close_context(ecc);
        channel_teardown(channel);
        auntie_msg_destroy(msg2);
        return NULL;
    }
    synack = auntie_msg_get_payload(msg2);

    /* Compute the shared secret x(g^{ab}) (the SGX API only returns the x-coordinate
     * of the shared point) and immediately release the ECC context */
    status = sgx_ecc256_compute_shared_dhkey(&a, &synack->gb, &gab, ecc);
    sgx_ecc256_close_context(ecc);
    if (SGX_SUCCESS != status) {
        printf("%s: sgx_ecc256_compute_shared_dhkey failed with error 0x%x\n", \
            __func__, status);
        channel_teardown(channel);
        auntie_msg_destroy(msg2);
        return NULL;
    }

    /* Save the operator's TEE's quote */
    operator_quote_size = auntie_msg_get_payload_size(msg2) - sizeof(synack->gb) - sizeof(synack->nonce);
    operator_quote = malloc(operator_quote_size);
    if (!operator_quote) {
        printf("%s: failed to allocate buffer for the operator's TEE's quote of size %u\n", \
            __func__, operator_quote_size);
        channel_teardown(channel);
        auntie_msg_destroy(msg2);
        return NULL;
    }
    (void) memcpy(operator_quote, synack->quote, operator_quote_size);
    printf("%s: got operator quote of size %lu\n", __func__, operator_quote_size);

    /* Derive a session key */
    ret = get_session_key(&session_key, &ga, &synack->gb, &gab);
    if (ret) {
        printf("%s: failed to derive session key with error %d\n", __func__, ret);
        channel_teardown(channel);
        auntie_msg_destroy(msg2);
        return NULL;
    }

    printf("%s: switching to encrypted communication\n", __func__);
    /* Switch to encrypted communication */
    channel_encrypt(channel, &session_key);

    /* Bind the Diffie-Hellman exchange and the nonce to the hardware report */
    ret = get_report_data(&player_report_data, synack->nonce, &ga, &synack->gb);
    if (ret) {
        printf("%s: failed to derive player's report data with error %d\n", __func__, ret);
        channel_teardown(channel);
        return NULL;
    }
    /* Derive the report data that the operator was suppoed to use for their report */
    ret = get_report_data(&operator_report_data, nonce, &ga, &synack->gb);
    /* We do not need the SYN/ACK message past this point */
    auntie_msg_destroy(msg2);
    if (ret) {
        printf("%s: failed to derive operator's report data with error %d\n", __func__, ret);
        channel_teardown(channel);
        return NULL;
    }

    /* Create the report */
    status = sgx_create_report(quoting_enclave, &player_report_data, &report);
    if (SGX_SUCCESS != status) {
        printf("%s: sgx_create_report failed with error 0x%x\n", __func__, status);
        channel_teardown(channel);
        return NULL;
    }

    printf("%s: requesting local quote\n", __func__);
    ret = OCALL(ocall_quote_me, &report);
    if (ret) {
        printf("%s: ocall_quote_me failed with error %d\n", __func__, ret);
        channel_teardown(channel);
        return NULL;
    }

    /* By this point, the quote should have been stashed in stashed_quote in ecall_quote_me_callback */
    if (!stashed_quote || !stashed_quote_size) {
        /* The host behaved maliciously and returned 0 from ocall_quote_me while
         * not having executed ecall_quote_me_callback */
        printf("%s: quote not stashed after returning from ocall_quote_me\n", __func__);
        channel_teardown(channel);
        return NULL;
    }

    printf("%s: sending the ACK message\n", __func__);
    /* Create the ACK message */
    msg3 = auntie_msg_create(AUNTIE_MSG_ATTESTATION_ACK, sizeof(struct auntie_msg_attestation_ack) + stashed_quote_size);
    if (!msg3) {
        printf("%s: failed to create the ACK message\n", __func__);
        channel_teardown(channel);
        drop_stashed_quote();
        return NULL;
    }

    ack = auntie_msg_get_payload(msg3);
    (void) memcpy(ack->quote, stashed_quote, stashed_quote_size);
    /* We no longer need our own quote */
    drop_stashed_quote();

    ret = auntie_msg_send(msg3, channel);
    auntie_msg_destroy(msg3);
    if (ret) {
        printf("%s: failed to send ACK message with error %d\n", __func__, ret);
        channel_teardown(channel);
        return NULL;
    }

    operator = malloc(sizeof(*operator));
    if (!operator) {
        channel_teardown(channel);
        return NULL;
    }
    operator->channel = channel;
    operator->quote = operator_quote;
    operator->quote_length = operator_quote_size;
    operator->report_data = operator_report_data;

    printf("%s: established a channel with the operator, received quote of length %lu\n", __func__, operator->quote_length);

    return operator;
}
