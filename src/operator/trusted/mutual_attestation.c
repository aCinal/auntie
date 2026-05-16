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

struct player *mutual_attestation(void *context, sgx_target_info_t *quoting_enclave)
{
    sgx_report_t report;
    sgx_report_data_t player_report_data;
    sgx_report_data_t operator_report_data;
    sgx_ecc_state_handle_t ecc;
    sgx_ec256_private_t b;
    sgx_ec256_public_t gb;
    sgx_ec256_dh_shared_t gab;
    sgx_status_t status;
    sgx_key_128bit_t session_key;
    struct auntie_msg *msg1, *msg2, *msg3;
    struct auntie_msg_attestation_syn *syn;
    struct auntie_msg_attestation_synack *synack;
    struct auntie_msg_attestation_ack *ack;
    struct channel *channel;
    struct player *player;
    uint8_t nonce[ATTESTATION_NONCE_SIZE];
    uint8_t *player_quote;
    uint32_t player_quote_size;
    int ret;

    /* Choose a nonce for the player's attestation */
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

    printf("%s: waiting for the SYN message\n", __func__);

    /* Receive the SYN message (Ga, nonce) */
    msg1 = auntie_msg_receive(channel);
    if (!msg1) {
        printf("%s: failed to receive the SYN message\n", __func__);
        channel_teardown(channel);
        return NULL;
    }

    if (auntie_msg_get_type(msg1) != AUNTIE_MSG_ATTESTATION_SYN) {
        printf("%s: received unexpected message %u (expected: %u)\n", \
            __func__, auntie_msg_get_type(msg1), AUNTIE_MSG_ATTESTATION_SYN);
        channel_teardown(channel);
        auntie_msg_destroy(msg1);
        return NULL;
    }

    if (auntie_msg_get_payload_size(msg1) != sizeof(struct auntie_msg_attestation_syn)) {
        printf("%s: unexpected payload size %u for the SYN message (expected: %u)\n", \
            __func__, auntie_msg_get_payload_size(msg1), sizeof(struct auntie_msg_attestation_syn));
        channel_teardown(channel);
        auntie_msg_destroy(msg1);
        return NULL;
    }
    syn = auntie_msg_get_payload(msg1);

    /* Verify the ID the player sent us is ok */
    if (!player_id_ok(syn->id)) {
        printf("%s: invalid player ID %u\n", __func__, syn->id);
        channel_teardown(channel);
        auntie_msg_destroy(msg1);
        return NULL;
    }

    channel_assign_id(channel, syn->id);
    printf("%s: establishing channel with player %u\n", __func__, syn->id);

    /* Create our own secp256r1 Diffie-Hellman share */
    status = sgx_ecc256_open_context(&ecc);
    if (SGX_SUCCESS != status) {
        printf("%s: sgx_ecc256_open_context failed with error 0x%x\n", \
            __func__, status);
        channel_teardown(channel);
        auntie_msg_destroy(msg1);
        return NULL;
    }

    status = sgx_ecc256_create_key_pair(&b, &gb, ecc);
    if (SGX_SUCCESS != status) {
        printf("%s: sgx_ecc256_create_key_pair failed with error 0x%x\n", \
            __func__, status);
        sgx_ecc256_close_context(ecc);
        channel_teardown(channel);
        auntie_msg_destroy(msg1);
        return NULL;
    }

    /* Compute the shared secret x(g^{ab}) (the SGX API only returns the x-coordinate
     * of the shared point) and immediately release the ECC context */
    status = sgx_ecc256_compute_shared_dhkey(&b, &syn->ga, &gab, ecc);
    sgx_ecc256_close_context(ecc);
    if (SGX_SUCCESS != status) {
        printf("%s: sgx_ecc256_compute_shared_dhkey failed with error 0x%x\n", \
            __func__, status);
        channel_teardown(channel);
        auntie_msg_destroy(msg1);
        return NULL;
    }

    /* Derive a session key */
    ret = get_session_key(&session_key, &syn->ga, &gb, &gab);
    if (ret) {
        printf("%s: failed to derive session key with error %d\n", __func__, ret);
        channel_teardown(channel);
        auntie_msg_destroy(msg1);
        return NULL;
    }

    /* Bind the Diffie-Hellman exchange and the nonce to the hardware report */
    ret = get_report_data(&operator_report_data, syn->nonce, &syn->ga, &gb);
    if (ret) {
        printf("%s: failed to derive operator's report data with error %d\n", __func__, ret);
        channel_teardown(channel);
        auntie_msg_destroy(msg1);
        return NULL;
    }
    /* Derive the report data that the player is suppoed to use for their report */
    ret = get_report_data(&player_report_data, nonce, &syn->ga, &gb);
    /* We do not need the SYN message past this point */
    auntie_msg_destroy(msg1);
    if (ret) {
        printf("%s: failed to derive player's report data with error %d\n", __func__, ret);
        channel_teardown(channel);
        return NULL;
    }

    /* Create the report */
    status = sgx_create_report(quoting_enclave, &operator_report_data, &report);
    if (SGX_SUCCESS != status) {
        printf("%s: sgx_create_report failed with error 0x%x\n", __func__, status);
        channel_teardown(channel);
        auntie_msg_destroy(msg1);
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

    printf("%s: sending the SYN/ACK message\n", __func__);
    /* Create the SYN/ACK message */
    msg2 = auntie_msg_create(AUNTIE_MSG_ATTESTATION_SYNACK, sizeof(struct auntie_msg_attestation_synack) + stashed_quote_size);
    if (!msg2) {
        printf("%s: failed to create the SYN/ACK message\n", __func__);
        channel_teardown(channel);
        drop_stashed_quote();
        return NULL;
    }

    synack = auntie_msg_get_payload(msg2);
    synack->gb = gb;
    (void) memcpy(synack->nonce, nonce, sizeof(synack->nonce));
    (void) memcpy(synack->quote, stashed_quote, stashed_quote_size);
    /* We no longer need our own quote */
    drop_stashed_quote();

    /* TODO: The operator's quote is sent in plaintext for now, switch to encrypted communication first */
    ret = auntie_msg_send(msg2, channel);
    auntie_msg_destroy(msg2);
    if (ret) {
        printf("%s: failed to send SYN/ACK message with error %d\n", __func__, ret);
        channel_teardown(channel);
        return NULL;
    }

    printf("%s: switching to encrypted communication\n", __func__);
    /* Switch to encrypted communication */
    channel_encrypt(channel, &session_key);

    printf("%s: waiting for the ACK message\n", __func__);

    /* Receive the ACK message */
    msg3 = auntie_msg_receive(channel);
    if (!msg3) {
        printf("%s: failed to receive the ACK message\n", __func__);
        channel_teardown(channel);
        return NULL;
    }

    if (auntie_msg_get_type(msg3) != AUNTIE_MSG_ATTESTATION_ACK) {
        printf("%s: received unexpected message %u (expected: %u)\n", \
            __func__, auntie_msg_get_type(msg3), AUNTIE_MSG_ATTESTATION_ACK);
        channel_teardown(channel);
        auntie_msg_destroy(msg3);
        return NULL;
    }

    /* Save the player's TEE's quote */
    player_quote_size = auntie_msg_get_payload_size(msg3);
    player_quote = malloc(player_quote_size);
    if (!player_quote) {
        channel_teardown(channel);
        auntie_msg_destroy(msg3);
        return NULL;
    }
    ack = auntie_msg_get_payload(msg3);
    (void) memcpy(player_quote, ack->quote, auntie_msg_get_payload_size(msg3));
    auntie_msg_destroy(msg3);

    /* Create a player object */
    player = calloc(1, sizeof(*player));
    if (!player) {
        channel_teardown(channel);
        auntie_msg_destroy(msg3);
        return NULL;
    }

    player->channel = channel;
    player->id = channel_id(channel);
    player->quote = player_quote;
    player->quote_length = player_quote_size;
    player->report_data = player_report_data;

    printf("%s: established a channel with player %lu, received quote of length %lu\n", \
        __func__, player->id, player->quote_length);

    return player;
}
