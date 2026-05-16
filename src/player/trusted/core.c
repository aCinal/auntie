#include "core.h"
#include "zcash.h"
#include "messages.h"
#include "core_common.h"
#include "setup.h"
#include "ocall.h"
#include <string.h>
#include <errno.h>

static struct zcash_wallet *deposit_wallet;
static struct zcash_wallet *payout_wallet;
static uint8_t *functionality_output;
static size_t functionality_output_length;
static struct zcash_advice *deposit_advice;
static uint8_t settlement_sighash[32];

int ecall_initialize_impl(void *context)
{
    uint8_t *deposit_address;
    size_t deposit_address_length;
    int ret;

    deposit_wallet = zcash_create_wallet();
    if (!deposit_wallet) {
        printf("%s: failed to create deposit wallet\n", __func__);
        return -ENOMEM;
    }
    payout_wallet = zcash_create_wallet();
    if (!payout_wallet) {
        printf("%s: failed to create payout wallet\n", __func__);
        zcash_release_wallet(deposit_wallet);
        deposit_wallet = NULL;
        return -ENOMEM;
    }

    ret = zcash_export_address(&deposit_address, &deposit_address_length, deposit_wallet->address);
    if (ret) {
        printf("%s: failed to export deposit address with error %d\n", __func__, ret);
        zcash_release_wallet(deposit_wallet);
        zcash_release_wallet(payout_wallet);
        deposit_wallet = NULL;
        payout_wallet = NULL;
        return ret;
    }

    /* Output the deposit address for the player */
    ret = OCALL(ocall_initialize_callback, context, deposit_address, deposit_address_length);
    free(deposit_address);
    if (ret) {
        printf("%s: ocall_initialize_callback failed with error %d\n", __func__, ret);
        zcash_release_wallet(deposit_wallet);
        zcash_release_wallet(payout_wallet);
        deposit_wallet = NULL;
        payout_wallet = NULL;
    }

    printf("%s: successfully initialized the node\n", __func__);

    return ret;
}

int ecall_deposit_and_input_impl(const uint8_t *deposit_transaction, size_t deposit_transaction_length, const uint8_t *input, size_t input_length)
{
    struct zcash_transaction *tx;
    zat_t deposit_amount;
    struct auntie_msg *msg;
    struct auntie_msg_deposit_and_input *payload;
    uint32_t payload_size;
    uint8_t *payout_address;
    size_t payout_address_length;
    uint8_t *raw_advice;
    size_t raw_advice_length;
    int ret;

    /* Import the transaction thus also verifying it is well-formatted */
    tx = zcash_import_transaction(deposit_transaction, deposit_transaction_length);
    if (!tx) {
        printf("%s: failed to import deposit transaction\n", __func__);
        return -EINVAL;
    }
    /* Get the amount deposited to the our deposit address */
    deposit_amount = zcash_deposited_amount(tx, deposit_wallet->key);
    /* We no longer need the transaction past this point */
    zcash_release_transaction(tx);

    ret = zcash_export_address(&payout_address, &payout_address_length, payout_wallet->address);
    if (ret) {
        printf("%s: failed to export payout address with error %d\n", __func__, ret);
        return ret;
    }

    deposit_advice = zcash_create_advice(deposit_wallet->key);
    if (!deposit_advice) {
        printf("%s: failed to get own advice\n", __func__);
        free(payout_address);
        return -EFAULT;
    }

    ret = zcash_export_advice(&raw_advice, &raw_advice_length, deposit_advice);
    if (ret) {
        printf("%s: failed to export own advice with error %d\n", __func__, ret);
        free(payout_address);
        zcash_release_advice(deposit_advice);
        deposit_advice = NULL;
        return ret;
    }

    printf("%s: sending the deposit transaction, the functionality's input, the payout address and the advice to the operator's TEE\n", __func__);

    /* Send a message to the operator's TEE */
    payload_size = \
        sizeof(struct auntie_msg_deposit_and_input) + \
        deposit_transaction_length + \
        input_length + \
        payout_address_length + \
        raw_advice_length;
    msg = auntie_msg_create(AUNTIE_MSG_DEPOSIT_AND_INPUT, payload_size);
    if (!msg) {
        printf("%s: failed to create deposit-and-input message of size %u\n", __func__, payload_size);
        free(payout_address);
        free(raw_advice);
        zcash_release_advice(deposit_advice);
        deposit_advice = NULL;
        return -ENOMEM;
    }
    payload = auntie_msg_get_payload(msg);
    payload->deposit_amount = deposit_amount;
    payload->deposit_transaction_offset = 0;
    payload->input_offset = payload->deposit_transaction_offset + deposit_transaction_length;
    payload->payout_address_offset = payload->input_offset + input_length;
    payload->advice_offset = payload->payout_address_offset + payout_address_length;
    (void) memcpy(payload->data + payload->deposit_transaction_offset, deposit_transaction, deposit_transaction_length);
    (void) memcpy(payload->data + payload->input_offset, input, input_length);
    (void) memcpy(payload->data + payload->payout_address_offset, payout_address, payout_address_length);
    (void) memcpy(payload->data + payload->advice_offset, raw_advice, raw_advice_length);
    ret = auntie_msg_send(msg, operator->channel);
    auntie_msg_destroy(msg);
    free(payout_address);
    free(raw_advice);
    if (ret) {
        printf("%s: failed to send deposit-and-input message to operator's TEE with error %d\n", __func__, ret);
        zcash_release_advice(deposit_advice);
        deposit_advice = NULL;
        return ret;
    }

    printf("%s: deposit made successfully\n", __func__);

    /* Note that we could actually wait here for the response from the operator, but, to be consistent
     * with the paper, return and have the host call GetDeposits */
    return 0;
}

int ecall_get_deposits_impl(void *context)
{
    struct auntie_msg *msg;
    struct auntie_msg_clear_contract *payload;
    int ret;

    printf("%s: receiving the unauthorized settlement transaction's hash, the functionality's output, and the counterparties' deposit transactions from the operator\n", __func__);

    /* Receive the unauthorized settlement transaction's hash, the functionality's
     * output, and the deposit transactions from the operator's TEE */
    msg = auntie_msg_receive(operator->channel);
    if (!msg) {
        printf("%s: failed to receive clear-contract message from the operator\n", __func__);
        return -EIO;
    }
    if (auntie_msg_get_type(msg) != AUNTIE_MSG_CLEAR_CONTRACT) {
        printf("%s: unexpected message %u received (expected: %u)\n", \
            __func__, auntie_msg_get_type(msg), AUNTIE_MSG_CLEAR_CONTRACT);
        auntie_msg_destroy(msg);
        return -EIO;
    }
    payload = auntie_msg_get_payload(msg);

    /* Save the settlement transaction's hash */
    (void) memcpy(settlement_sighash, payload->settlement_sighash, sizeof(settlement_sighash));

    /* Save the output of the functionality */
    functionality_output_length = payload->deposit_transactions_offsets[0] - payload->output_offset;
    functionality_output = malloc(functionality_output_length);
    /* NOTE: the length of the functionality's output is likely leaked here
     *       (some information about it was already leaked by the size of msg) */
    if (!functionality_output) {
        printf("%s: failed to allocate a buffer for functionality's output\n", __func__);
        auntie_msg_destroy(msg);
        return -ENOMEM;
    }
    (void) memcpy(functionality_output, payload->data + payload->output_offset, functionality_output_length);

    printf("%s: outputting the deposit transactions for inspection\n", __func__);

    /* Output the deposits one by one */
    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++) {
        ret = OCALL(ocall_get_deposits_callback,
            context,
            payload->data + payload->deposit_transactions_offsets[i],
            payload->deposit_transactions_offsets[i + 1] - payload->deposit_transactions_offsets[i]
        );
        if (ret) {
            printf("%s: ocall_get_deposits_callback failed with error %d\n", __func__, ret);
            /* There is nothing better to do than abort */
            auntie_msg_destroy(msg);
            free(functionality_output);
            functionality_output = NULL;
            return ret;
        }
    }
    ret = OCALL(ocall_get_deposits_callback,
        context,
        payload->data + payload->deposit_transactions_offsets[AUNTIE_NUM_PLAYERS],
        auntie_msg_get_payload_size(msg) - sizeof(*payload) - payload->deposit_transactions_offsets[AUNTIE_NUM_PLAYERS]
    );
    auntie_msg_destroy(msg);
    if (ret) {
        printf("%s: ocall_get_deposits_callback failed with error %d\n", __func__, ret);
        /* There is nothing better to do than abort */
        free(functionality_output);
        functionality_output = NULL;
    }

    return 0;
}

int ecall_confirm_deposits_impl(void)
{
    struct zcash_authorization *signature;
    struct auntie_msg *msg;
    struct auntie_msg_confirm_deposits *payload;
    uint8_t *raw_signature;
    size_t raw_signature_length;
    int ret;

    printf("%s: signing the settlement transaction thus confirming all deposits have been made\n", __func__);

    /* Sign the settlement transaction and send the signature to the operator */
    signature = zcash_sign_transaction(deposit_wallet->key, deposit_advice, settlement_sighash);
    /* We can now release the advice */
    zcash_release_advice(deposit_advice);
    deposit_advice = NULL;
    if (!signature) {
        printf("%s: failed to sign settlement transaction\n", __func__);
        return -EFAULT;
    }

    printf("%s: exporting the signature\n", __func__);
    ret = zcash_export_signature(&raw_signature, &raw_signature_length, signature);
    zcash_release_signature(signature);
    if (ret) {
        printf("%s: failed to export signature on settlement transaction with error %d\n", __func__, ret);
        return ret;
    }

    printf("%s: sending confirm deposits message\n", __func__);
    msg = auntie_msg_create(AUNTIE_MSG_CONFIRM_DEPOSITS, sizeof(struct auntie_msg_confirm_deposits) + raw_signature_length);
    if (!msg) {
        printf("%s: failed to create confirm-deposits message\n", __func__);
        free(raw_signature);
        return -ENOMEM;
    }
    payload = auntie_msg_get_payload(msg);
    payload->signature_length = raw_signature_length;
    (void) memcpy(payload->signature, raw_signature, raw_signature_length);

    ret = auntie_msg_send(msg, operator->channel);
    auntie_msg_destroy(msg);
    free(raw_signature);
    if (ret) {
        printf("%s: failed to send confirm-deposits message with error %d\n", __func__, ret);
        return ret;
    }

    printf("%s: deposits confirmed\n", __func__);

    return 0;
}

int ecall_settle_impl(void *context, const uint8_t *blocks, size_t blocks_length)
{
    /* We have to be careful here - if the OCALL that prints the key and the output returns error, we should
     * not trust it and should still transition to state SETTLED; otherwise, a player would be able to get
     * _both_ a refund and the functionality's output! (The refund would be worthless at that point as it
     * takes a longer chain of blocks to call this function than to call Refund, but we nevertheless take
     * the extra precaution.) */

    struct zcash_blocks *chain;
    uint8_t *payout_key;
    size_t payout_key_length;
    int ret;

    chain = zcash_import_blocks(blocks, blocks_length);
    if (!chain) {
        printf("%s: failed to import blocks\n", __func__);
        return -EINVAL;
    }

    ret = zcash_authorized_and_buried(settlement_sighash, deposit_wallet->key, chain);
    zcash_release_blocks(chain);
    if (ret < 0) {
        printf("%s: failed to verify chain with error %d\n", __func__, ret);
        return ret;
    }

    if (ret < AUNTIE_SETTLE_DELAY_BLOCKS) {
        printf("%s: too soon to ask for the payout and output\n", __func__);
        return -EAGAIN;
    }

    printf("%s: settlement ok, releasing the payout key and the functionality's output\n", __func__);

    ret = zcash_export_key(&payout_key, &payout_key_length, payout_wallet->key);
    if (ret) {
        printf("%s: failed to export the payout key with error %d\n", __func__, ret);
        return ret;
    }

    /* NOTE: We cannot trust this OCALL's return value, make the best effort printing the output and key,
     *       but if the host says there was an error, too bad, we cannot risk their being malicious
     *       and must do the correct state transition anyway */
    (void) OCALL(ocall_settle_callback, context, payout_key, payout_key_length, functionality_output, functionality_output_length);

    free(payout_key);

    return 0;
}

int ecall_refund_impl(void *context, const uint8_t *blocks, size_t blocks_length)
{
    /* We have to be careful here - if the OCALL that prints the key returns error, we should not trust it
     * and should still transition to state REFUNDED; otherwise, a player would be able to get _both_ a refund
     * and the functionality's output some time later! */

    struct zcash_blocks *chain;
    uint8_t *deposit_key;
    size_t deposit_key_length;
    int ret;

    chain = zcash_import_blocks(blocks, blocks_length);
    if (!chain) {
        printf("%s: failed to import blocks\n", __func__);
        return -EINVAL;
    }

    ret = zcash_blocks_since_checkpoint(chain);
    zcash_release_blocks(chain);
    if (ret < 0) {
        printf("%s: failed to verify chain with error %d\n", __func__, ret);
        return ret;
    }

    if (ret < AUNTIE_REFUND_DELAY_BLOCKS) {
        printf("%s: too soon to ask for refund\n", __func__);
        return -EAGAIN;
    }

    printf("%s: refund request approved, releasing the deposit key\n", __func__);

    ret = zcash_export_key(&deposit_key, &deposit_key_length, deposit_wallet->key);
    if (ret) {
        printf("%s: failed to export the deposit key with error %d\n", __func__, ret);
        return ret;
    }

    /* NOTE: We cannot trust this OCALL's return value, make the best effort printing the output and key,
     *       but if the host says there was an error, too bad, we cannot risk their being malicious
     *       and must do the correct state transition anyway */
    (void) OCALL(ocall_refund_callback, context, deposit_key, deposit_key_length);

    free(deposit_key);

    return 0;
}
