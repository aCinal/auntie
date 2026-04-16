#include "zcash.h"
#include "printf.h"

// TODO: Implement actual interface to orchard
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// TODO: Map to struct SpendingKey in Orchard
struct zcash_key { int dummy; };
// TODO: Map to struct Address in Orchard (choose random diversifier?)
struct zcash_address { int dummy; };
// TODO: Map to struct Bundle in Orchard
struct zcash_transaction { int dummy; };
// TODO: Add authorization for an action description (struct Action in Orchard).
//       Either each action description will need to be of the form (ith deposit, ith payout),
//       where both the spend and the mint correspond to the same player, or the TEEs
//       will need to exchange full viewing keys to be able to produce Halo2 zk-SNARKs
//       on behalf of each other (the latter will have consequences for privacy under
//       side-channel leakage)
struct zcash_authorization { int dummy; };
struct zcash_blocks { int dummy; };

static uint8_t checkpoint_block[] = "DUMMY";

int dummy_counter;

struct zcash_wallet *zcash_create_wallet(void)
{
    struct zcash_wallet *wallet;

    // TODO: Allocate struct SpendingKey and struct Address

    // TODO: Be careful about side channels here

    wallet = malloc(sizeof(struct zcash_wallet));
    if (!wallet)
        return NULL;

    wallet->address = malloc(sizeof(struct zcash_address));
    if (!wallet->address) {
        free(wallet);
        return NULL;
    }
    wallet->key = malloc(sizeof(struct zcash_key));
    if (!wallet->key) {
        free(wallet->address);
        free(wallet);
        return NULL;
    }

    wallet->key->dummy = dummy_counter++;
    wallet->address->dummy = dummy_counter++;

    printf("%s stub returning wallet with key=%d, address=%d\n", \
        __func__, wallet->key->dummy, wallet->address->dummy);

    return wallet;
}

void zcash_destroy_wallet(struct zcash_wallet *wallet)
{
    printf("%s stub destroyed wallet with key=%d, address=%d!\n", \
        __func__, wallet->key->dummy, wallet->address->dummy);
    free(wallet->address);
    free(wallet->key);
    free(wallet);
}

int zcash_export_key(uint8_t **raw_key, size_t *raw_key_length, const struct zcash_key *key)
{
#define STUB_RAW_KEY  "STUB_RAW_ZCASH_KEY"

    printf("%s stub called with key=%d!\n", __func__, key->dummy);

    *raw_key = malloc(sizeof(STUB_RAW_KEY));
    if (!*raw_key)
        return -ENOMEM;
    (void) memcpy(*raw_key, STUB_RAW_KEY, sizeof(STUB_RAW_KEY));
    *raw_key_length = sizeof(STUB_RAW_KEY);

    return 0;
}

struct zcash_address *zcash_import_address(const uint8_t *raw_address, size_t raw_address_length)
{
    struct zcash_address *address;
    printf("%s stub called!\n", __func__);
    (void) raw_address;
    (void) raw_address_length;

    address = malloc(sizeof(struct zcash_address));
    if (address) {
        address->dummy = dummy_counter++;
        printf("%s stub creating address=%d!\n", __func__, address->dummy);
    }

    return address;
}

int zcash_export_address(uint8_t **raw_address, size_t *raw_address_length, const struct zcash_address *address)
{
#define STUB_RAW_ADDRESS  "STUB_RAW_ZCASH_ADDRESS"

    printf("%s stub called with address=%d!\n", __func__, address->dummy);

    *raw_address = malloc(sizeof(STUB_RAW_ADDRESS));
    if (!*raw_address)
        return -ENOMEM;
    (void) memcpy(*raw_address, STUB_RAW_ADDRESS, sizeof(STUB_RAW_ADDRESS));
    *raw_address_length = sizeof(STUB_RAW_ADDRESS);

    return 0;
}

void zcash_release_address(struct zcash_address *address)
{
    printf("%s stub called with address=%d!\n", __func__, address->dummy);
    free(address);
}

struct zcash_transaction *zcash_import_transaction(const uint8_t *raw_transaction, size_t raw_transaction_length)
{
    struct zcash_transaction *tx;

    // TODO: Validate the transaction

    printf("%s stub called!\n", __func__);
    (void) raw_transaction;
    (void) raw_transaction_length;

    tx = malloc(sizeof(struct zcash_transaction));
    if (tx) {
        tx->dummy = dummy_counter++;
        printf("%s stub creating transaction=%d!\n", __func__, tx->dummy);
    }

    return tx;
}

int zcash_export_transaction(uint8_t **raw_transaction, size_t *raw_transaction_length, const struct zcash_transaction *transaction)
{
#define STUB_RAW_TX  "STUB_RAW_ZCASH_TX"

    printf("%s stub called with transaction=%d!\n", __func__, transaction->dummy);

    *raw_transaction = malloc(sizeof(STUB_RAW_TX));
    if (!*raw_transaction)
        return -ENOMEM;
    (void) memcpy(*raw_transaction, STUB_RAW_TX, sizeof(STUB_RAW_TX));
    *raw_transaction_length = sizeof(STUB_RAW_TX);

    return 0;
}

void zcash_release_transaction(struct zcash_transaction *transaction)
{
    printf("%s stub called with transaction=%d!\n", __func__, transaction->dummy);
    free(transaction);
}

struct zcash_transaction * zcash_create_transaction(
    struct zcash_transaction *const inputs[AUNTIE_NUM_PLAYERS + 1],
    zatoshis_t payouts[AUNTIE_NUM_PLAYERS + 1],
    struct zcash_address *const payout_addresses[AUNTIE_NUM_PLAYERS + 1]
)
{
    struct zcash_transaction *tx;

    // TODO: Create struct Bundle with AUNTIE_NUM_PLAYERS+1 action descriptions (some may be dummy if payouts are 0)

    printf("%s stub called with inputs: ", __func__);
    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++)
        printf("%d, ", inputs[i]->dummy);
    printf("%d and addresses: ", inputs[AUNTIE_NUM_PLAYERS]->dummy);
    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++)
        printf("%d, ", payout_addresses[i]->dummy);
    printf("%d!\n", payout_addresses[AUNTIE_NUM_PLAYERS]->dummy);

    tx = malloc(sizeof(struct zcash_transaction));
    if (!tx)
        return NULL;
    tx->dummy = dummy_counter++;
    printf("%s stub creating transaction=%d\n", __func__, tx->dummy);

    return tx;
}

zatoshis_t zcash_deposited_amount(const struct zcash_transaction *transaction, const struct zcash_key *key)
{
    // TODO: Be careful about side channels here

    // TODO: Find struct Action corresponding to the key, decrypt, and read the amount

    printf("%s stub called with transaction=%d and key=%d, returning %lu!\n", \
        __func__, transaction->dummy, key->dummy, AUNTIE_OPERATOR_COLLATERAL);
    return AUNTIE_OPERATOR_COLLATERAL;
}

struct zcash_authorization *zcash_sign_transaction(const struct zcash_key *deposit_key, const struct zcash_transaction *unauthorized_transaction)
{
    struct zcash_authorization *auth;
    printf("%s stub called with key=%d and transaction=%d!\n", __func__, deposit_key->dummy, unauthorized_transaction->dummy);

    // TODO: Be careful about side channels here

    // TODO: Produce authorization? Either signatures + zk-SNARK or just signature(s) if zk-SNARK produced by the operator's TEE (requires full viewing key disclosure)

    auth = malloc(sizeof(struct zcash_authorization));
    if (!auth)
        return NULL;
    auth->dummy = dummy_counter++;
    printf("%s stub creating signature=%d\n", __func__, auth->dummy);

    return auth;
}

struct zcash_authorization *zcash_import_signature(const uint8_t *raw_signature, size_t raw_signature_length)
{
    struct zcash_authorization *auth;
    printf("%s stub called!\n", __func__);

    auth = malloc(sizeof(struct zcash_authorization));
    if (auth) {
        auth->dummy = dummy_counter++;
        printf("%s stub creating signature=%d\n", __func__, auth->dummy);
    }

    return auth;
}

int zcash_export_signature(uint8_t **raw_signature, size_t *raw_signature_length, const struct zcash_authorization *signature)
{
#define STUB_RAW_AUTH  "STUB_RAW_ZCASH_AUTH"

    printf("%s stub called with signature=%d!\n", __func__, signature->dummy);

    *raw_signature = malloc(sizeof(STUB_RAW_AUTH));
    if (!*raw_signature)
        return -ENOMEM;
    (void) memcpy(*raw_signature, STUB_RAW_AUTH, sizeof(STUB_RAW_AUTH));
    *raw_signature_length = sizeof(STUB_RAW_AUTH);

    return 0;
}

struct zcash_transaction *zcash_authorize_transaction(const struct zcash_transaction *unauthorized_transaction, struct zcash_authorization *const signatures[AUNTIE_NUM_PLAYERS + 1])
{
    struct zcash_transaction *tx;

    // TODO: Add missing struct Authorization objects to each struct Action in struct Bundle

    printf("%s stub called with transaction=%d and signatures: ", __func__, unauthorized_transaction->dummy);
    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++)
        printf("%d, ", signatures[i]->dummy);
    printf("%d!\n", signatures[AUNTIE_NUM_PLAYERS]->dummy);

    tx = malloc(sizeof(struct zcash_transaction));
    if (!tx)
        return NULL;
    tx->dummy = dummy_counter++;
    printf("%s stub creating transaction=%d\n", __func__, tx->dummy);

    return tx;
}

void zcash_release_signature(struct zcash_authorization *signature)
{
    printf("%s stub called with signature=%d!\n", __func__, signature->dummy);
    free(signature);
}

struct zcash_blocks *zcash_import_blocks(const uint8_t *blocks, size_t blocks_length)
{
    struct zcash_blocks *chain;
    (void) blocks;
    (void) blocks_length;
    printf("%s stub called!\n", __func__);

    chain = malloc(sizeof(struct zcash_blocks));
    if (chain) {
        chain->dummy = dummy_counter++;
        printf("%s stub creating blocks=%d\n", __func__, chain->dummy);
    }

    return chain;
}

void zcash_release_blocks(struct zcash_blocks *blocks)
{
    printf("%s stub called with blocks=%d\n", __func__, blocks->dummy);
    free(blocks);
}

int zcash_authorized_and_buried(const struct zcash_transaction *unauthorized_transaction, const struct zcash_key *deposit_key, const struct zcash_blocks *blocks)
{
    // TODO: Look for the transaction in a time-independent manner (always taking O(|blocks|) time and not branching) - there is no reason to leak that the
    //       settlement transaction is there early (even though it's technically fine: the players will not be able to issue informed bribes)

    printf("%s stub called with transaction=%d, key=%d, and blocks=%d! Returning %d!\n", \
        __func__, unauthorized_transaction->dummy, deposit_key->dummy, blocks->dummy, AUNTIE_SETTLE_DELAY_BLOCKS);
    (void) checkpoint_block;
    // TODO: Be careful about side channels here
    return AUNTIE_SETTLE_DELAY_BLOCKS;
}

int zcash_blocks_since_checkpoint(const struct zcash_blocks *blocks)
{
    printf("%s stub called with blocks=%d! Returning %d!\n", __func__, blocks->dummy, AUNTIE_REFUND_DELAY_BLOCKS);
    (void) checkpoint_block;
    return AUNTIE_REFUND_DELAY_BLOCKS;
}
