#include "core.h"
#include "zcash.h"
#include "messages.h"
#include "core_common.h"
#include "players.h"
#include "functionality.h"
#include "ocall.h"
#include <errno.h>
#include <string.h>

static struct zcash_wallet *deposit_wallet;
static struct zcash_partial_transaction *unauthorized_settlement;
static struct zcash_authorization *operator_signature;

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

    ret = zcash_export_address(&deposit_address, &deposit_address_length, deposit_wallet->address);
    if (ret) {
        printf("%s: failed to export deposit address with error %d\n", __func__, ret);
        zcash_release_wallet(deposit_wallet);
        deposit_wallet = NULL;
        return ret;
    }

    /* Output the deposit address for the operator */
    ret = OCALL(ocall_initialize_callback, context, deposit_address, deposit_address_length);
    free(deposit_address);
    if (ret) {
        printf("%s: ocall_initialize_callback failed with error %d\n", __func__, ret);
        zcash_release_wallet(deposit_wallet);
        deposit_wallet = NULL;
        return ret;
    }

    printf("%s: successfully initialized the node\n", __func__);

    return ret;
}

int ecall_clear_contract_impl(
    void *context,
    const uint8_t *payout_address,
    size_t payout_address_length,
    const uint8_t *collateral_transaction,
    size_t collateral_transaction_length,
    const uint8_t *merkle_paths,
    size_t merkle_paths_length
)
{
    struct auntie_msg *msg;
    struct auntie_msg_deposit_and_input *deposit_payload;
    struct auntie_msg_clear_contract *clear_payload;
    uint8_t *inputs[AUNTIE_NUM_PLAYERS] = {};
    uint8_t *outputs[AUNTIE_NUM_PLAYERS] = {};
    size_t input_lengths[AUNTIE_NUM_PLAYERS];
    size_t output_lengths[AUNTIE_NUM_PLAYERS];
    zat_t collateral_amount;
    zat_t deposit_amounts[AUNTIE_NUM_PLAYERS];
    zat_t payouts[AUNTIE_NUM_PLAYERS + 1];
    struct zcash_transaction *deposit_transactions[AUNTIE_NUM_PLAYERS + 1] = {};
    struct zcash_address *payout_addresses[AUNTIE_NUM_PLAYERS + 1] = {};
    struct zcash_advice *advices[AUNTIE_NUM_PLAYERS + 1] = {};
    uint8_t *raw_deposit_transactions[AUNTIE_NUM_PLAYERS] = {};
    size_t raw_deposit_transactions_lengths[AUNTIE_NUM_PLAYERS];
    size_t total_deposit_transactions_length;
    uint32_t length;
    uint32_t offset;
    uint8_t settlement_sighash[32];
    int ret;

    payout_addresses[0] = zcash_import_address(payout_address, payout_address_length);
    if (!payout_addresses[0]) {
        printf("%s: failed to import external payout address\n", __func__);
        ret = -EINVAL;
        goto cleanup;
    }

    advices[0] = zcash_create_advice(deposit_wallet->key);
    if (!advices[0]) {
        printf("%s: failed to get our own advice\n", __func__);
        ret = -EFAULT;
        goto cleanup;
    }

    deposit_transactions[0] = zcash_import_transaction(collateral_transaction, collateral_transaction_length);
    if (!deposit_transactions[0]) {
        printf("%s: failed to import collateral transaction\n", __func__);
        ret = -EINVAL;
        goto cleanup;
    }
    total_deposit_transactions_length = collateral_transaction_length;

    collateral_amount = zcash_deposited_amount(deposit_transactions[0], deposit_wallet->key);
    if (AUNTIE_OPERATOR_COLLATERAL != collateral_amount) {
        printf("%s: collateral transaction deposits %lu zatoshi(s), expected %lu\n", \
            __func__, collateral_amount, AUNTIE_OPERATOR_COLLATERAL);
        ret = -EINVAL;
        goto cleanup;
    }

    printf("%s: collecting deposit transactions and inputs from the players\n", __func__);

    /* Collect deposit transactions and inputs from all players */
    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++) {
        msg = auntie_msg_receive(players[i]->channel);
        if (!msg) {
            printf("%s: failed to receive the deposit-and-input message on channel %u\n", \
                __func__, channel_id(players[i]->channel));
            ret = -EIO;
            goto cleanup;
        }

        if (auntie_msg_get_type(msg) != AUNTIE_MSG_DEPOSIT_AND_INPUT) {
            printf("%s: unexpected message %u received (expected: %u)\n", \
                __func__, auntie_msg_get_type(msg), AUNTIE_MSG_DEPOSIT_AND_INPUT);
            auntie_msg_destroy(msg);
            ret = -EIO;
            goto cleanup;
        }

        deposit_payload = auntie_msg_get_payload(msg);

        length = deposit_payload->input_offset - deposit_payload->deposit_transaction_offset;
        deposit_transactions[i + 1] = zcash_import_transaction(deposit_payload->data + deposit_payload->deposit_transaction_offset, length);
        if (!deposit_transactions[i + 1]) {
            printf("%s: failed to import deposit transaction of player %d\n", __func__, i+1);
            auntie_msg_destroy(msg);
            ret = -EINVAL;
            goto cleanup;
        }
        raw_deposit_transactions[i] = malloc(length);
        if (!raw_deposit_transactions[i]) {
            printf("%s: failed to allocate buffer for raw deposit transaction of player %d\n", __func__, i+1);
            auntie_msg_destroy(msg);
            ret = -ENOMEM;
            goto cleanup;
        }
        (void) memcpy(raw_deposit_transactions[i], deposit_payload->data + deposit_payload->deposit_transaction_offset, length);
        raw_deposit_transactions_lengths[i] = length;
        total_deposit_transactions_length += length;
        deposit_amounts[i] = deposit_payload->deposit_amount;

        length = deposit_payload->payout_address_offset - deposit_payload->input_offset;
        inputs[i] = malloc(length);
        if (!inputs[i]) {
            printf("%s: failed to allocate buffer for player %d's input\n", __func__, i+1);
            auntie_msg_destroy(msg);
            ret = -ENOMEM;
            goto cleanup;
        }
        input_lengths[i] = length;
        (void) memcpy(inputs[i], deposit_payload->data + deposit_payload->payout_address_offset, length);

        length = deposit_payload->advice_offset - deposit_payload->payout_address_offset;
        payout_addresses[i + 1] = zcash_import_address(deposit_payload->data + deposit_payload->payout_address_offset, length);
        if (!payout_addresses[i + 1]) {
            printf("%s: failed to import payout address of player %d\n", __func__, i + 1);
            auntie_msg_destroy(msg);
            ret = -EINVAL;
            goto cleanup;
        }

        length = auntie_msg_get_payload_size(msg) - sizeof(*deposit_payload) - deposit_payload->advice_offset;
        advices[i + 1] = zcash_import_advice(deposit_payload->data + deposit_payload->advice_offset, length);
        if (!advices[i + 1]) {
            printf("%s: failed to import advice of player %d\n", __func__, i + 1);
            auntie_msg_destroy(msg);
            ret = -EINVAL;
            goto cleanup;
        }

        auntie_msg_destroy(msg);
    }

    printf("%s: evaluating the functionality\n", __func__);

    /* Offset payouts by 1 as the operator only redeems their collateral */
    ret = evaluate_functionality(
        outputs,
        output_lengths,
        payouts + 1,
        inputs,
        input_lengths,
        deposit_amounts
    );
    if (ret) {
        printf("%s: failed to evaluate functionality with error %d\n", __func__, ret);
        goto cleanup;
    }
    /* Pay the operator back their collateral */
    payouts[0] = collateral_amount;

    printf("%s: issuing the settlement transaction\n", __func__);

    /* Issue the settlement transaction */
    unauthorized_settlement = zcash_create_transaction(deposit_transactions, payouts, payout_addresses, advices, merkle_paths, merkle_paths_length);
    if (!unauthorized_settlement) {
        printf("%s: failed to create settlement transaction\n", __func__);
        ret = -EFAULT;
        goto cleanup;
    }
    // TODO: Use the correct hash here, see https://zips.z.cash/zip-0244
    zcash_hash_transaction(settlement_sighash, unauthorized_settlement);

    printf("%s: sending the functionality's output and the unauthorized settlement transaction's hash to each player\n", __func__);

    /* Send a clear-contract message to every player */
    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++) {

        length = \
            sizeof(struct auntie_msg_clear_contract) + \
            output_lengths[i] + \
            total_deposit_transactions_length;

        msg = auntie_msg_create(AUNTIE_MSG_CLEAR_CONTRACT, length);
        if (!msg) {
            printf("%s: failed to create clear-contract message of size %lu for player %d\n",
                __func__, length, channel_id(players[i]->channel));
            ret = -ENOMEM;
            goto cleanup;
        }

        clear_payload = auntie_msg_get_payload(msg);
        (void) memcpy(clear_payload->settlement_sighash, settlement_sighash, sizeof(clear_payload->settlement_sighash));
        offset = 0;
        clear_payload->output_offset = offset;
        (void) memcpy(clear_payload->data + offset, outputs[i], output_lengths[i]);
        offset += output_lengths[i];
        clear_payload->deposit_transactions_offsets[0] = offset;
        /* Restore indexing as in the paper, i.e., have the operator be at index 0
         * and player i at index i */
        (void) memcpy(clear_payload->data + offset, collateral_transaction, collateral_transaction_length);
        offset += collateral_transaction_length;
        for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++) {
            clear_payload->deposit_transactions_offsets[i + 1] = offset;
            (void) memcpy(clear_payload->data + offset, raw_deposit_transactions[i], raw_deposit_transactions_lengths[i]);
            offset += raw_deposit_transactions_lengths[i];
        }

        ret = auntie_msg_send(msg, players[i]->channel);
        auntie_msg_destroy(msg);
        if (ret) {
            printf("%s: failed to send clear-contract message on channel %u\n", \
                __func__, channel_id(players[i]->channel));
            goto cleanup;
        }
    }

    printf("%s: outputting the deposits for the host\n", __func__);

    /* Output the deposits one by one */
    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++) {
        ret = OCALL(ocall_clear_contract_callback,
            context,
            raw_deposit_transactions[i],
            raw_deposit_transactions_lengths[i]
        );
        if (ret) {
            printf("%s: ocall_clear_contract_callback failed with error %d\n", __func__, ret);
            goto cleanup;
        }
    }

    /* Sign the settlement before releasing our advice */
    operator_signature = zcash_sign_transaction(deposit_wallet->key, advices[0], settlement_sighash);
    if (!operator_signature) {
        printf("%s: failed to produce the operator's signature\n", __func__);
        ret = -EFAULT;
        goto cleanup;
    }

    printf("%s: contract cleared successfully\n", __func__);

cleanup:
    for (int i = 0; i < AUNTIE_NUM_PLAYERS + 1; i++) {
        if (deposit_transactions[i])
            zcash_release_transaction(deposit_transactions[i]);
        if (payout_addresses[i])
            zcash_release_address(payout_addresses[i]);
        if (advices[i])
            zcash_release_advice(advices[i]);
    }

    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++) {
        if (raw_deposit_transactions[i])
            free(raw_deposit_transactions[i]);
        if (inputs[i])
            free(inputs[i]);
        if (outputs[i])
            free(outputs[i]);
    }

    /* On error, release the settlement transaction */
    if (ret && unauthorized_settlement) {
        zcash_release_partial_transaction(unauthorized_settlement);
        unauthorized_settlement = NULL;
    }

    return ret;
}

int ecall_finalize_impl(void *context)
{
    /* We have to be super careful here - if the OCALL that prints the settlement transaction returns error,
     * we cannot trust it, and must still transition to state FINALIZED; otherwise, the operator would be
     * able to peek at the settlement transaction and then still back out of the contract and reclaim collateral */

    struct auntie_msg *msg;
    struct auntie_msg_confirm_deposits *payload;
    struct zcash_authorization *signatures[AUNTIE_NUM_PLAYERS + 1] = {};
    int ret;
    struct zcash_transaction *authorized_settlement = NULL;
    uint8_t *raw_settlement_transaction;
    size_t raw_settlement_transaction_length;

    printf("%s: waiting for the players to confirm the deposits\n", __func__);

    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++) {
        msg = auntie_msg_receive(players[i]->channel);
        if (!msg) {
            printf("%s: failed to receive the confirm-deposits message on channel %u\n", \
                __func__, channel_id(players[i]->channel));
            ret = -EIO;
            goto cleanup;
        }

        if (auntie_msg_get_type(msg) != AUNTIE_MSG_CONFIRM_DEPOSITS) {
            printf("%s: unexpected message %u received (expected: %u)\n", \
                __func__, auntie_msg_get_type(msg), AUNTIE_MSG_CONFIRM_DEPOSITS);
            auntie_msg_destroy(msg);
            ret = -EIO;
            goto cleanup;
        }

        payload = auntie_msg_get_payload(msg);
        signatures[i + 1] = zcash_import_signature(payload->signature, payload->signature_length);
        auntie_msg_destroy(msg);
        if (!signatures[i + 1]) {
            printf("%s: failed to import signature of player %d\n", __func__, channel_id(players[i]->channel));
            ret = -EINVAL;
            goto cleanup;
        }
    }

    /* Add the operator's signature */
    signatures[0] = operator_signature;

    printf("%s: all deposits confirmed by everyone, authorizing the settlement transaction\n", __func__);

    authorized_settlement = zcash_authorize_transaction(unauthorized_settlement, signatures);
    /* zcash_authorize_transaction consumes unauthorized_settlement */
    unauthorized_settlement = NULL;
    if (!authorized_settlement) {
        printf("%s: failed to authorize settlement\n", __func__);
        ret = -EINVAL;
        goto cleanup;
    }

    ret = zcash_export_transaction(&raw_settlement_transaction, &raw_settlement_transaction_length, authorized_settlement);
    if (ret) {
        printf("%s: failed to export fully authorized settlement transaction with error %d\n", __func__, ret);
        goto cleanup;
    }

    /* NOTE: We cannot trust this OCALL's return value, make the best effort printing the transaction,
     *       but if the host says there was an error, too bad, we cannot risk their being malicious
     *       and must do the correct state transition anyway */
    (void) OCALL(ocall_finalize_callback, context, raw_settlement_transaction, raw_settlement_transaction_length);

    printf("%s: contract finalized\n", __func__);

cleanup:
    for (int i = 0; i < AUNTIE_NUM_PLAYERS + 1; i++)
        if (signatures[i])
            zcash_release_signature(signatures[i]);

    if (authorized_settlement)
        zcash_release_transaction(authorized_settlement);

    return ret;
}

int ecall_refund_impl(void *context, const uint8_t *blocks, size_t blocks_length)
{
    /* We have to be super careful here - if the OCALL that prints the key returns error, we cannot trust it,
     * and must still transition to state REFUNDED; otherwise,  a player would be able to get _both_ a refund
     * and the functionality's output! */

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

    /* NOTE: We cannot trust this OCALL's return value, make the best effort printing the key,
     *       but if the host says there was an error, too bad, we cannot risk their being malicious
     *       and must do the correct state transition anyway */
    (void) OCALL(ocall_refund_callback, context, deposit_key, deposit_key_length);

    free(deposit_key);

    return 0;
}
