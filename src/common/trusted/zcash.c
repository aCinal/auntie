#include "zcash.h"
#include "printf.h"
#include <stdlib.h>
#include <errno.h>

/* Rust FFI */
extern struct zcash_key *zcash_create_key(void);
extern void zcash_release_key(struct zcash_key *key);
extern struct zcash_address *zcash_derive_address(const struct zcash_key *key);
extern void zcash_export_key_impl(uint8_t *raw_key, const struct zcash_key *key);
extern void zcash_export_address_impl(uint8_t *raw_address, const struct zcash_address *address);
extern void zcash_export_advice_impl(uint8_t *raw_advice, const struct zcash_advice *advice);
extern size_t zcash_get_raw_transaction_length(const struct zcash_transaction *transaction);
extern void zcash_export_transaction_impl(uint8_t *raw_transaction, const struct zcash_transaction *transaction);
extern void zcash_export_signature_impl(uint8_t *raw_signature, const struct zcash_authorization *signature);

struct zcash_wallet *zcash_create_wallet(void)
{
    struct zcash_wallet *wallet;
    struct zcash_key *key;
    struct zcash_address *address;

    /* Both zcash_create_key and zcash_derive_address currently cannot fail
     * (not gracefully at least), but for the sake of future-proofing, check
     * that they returned non-NULL */
    key = zcash_create_key();
    if (!key)
        return NULL;

    address = zcash_derive_address(key);
    if (!address) {
        zcash_release_key(key);
        return NULL;
    }

    wallet = malloc(sizeof(struct zcash_wallet));
    if (!wallet) {
        zcash_release_address(address);
        zcash_release_key(key);
        return NULL;
    }

    wallet->key = key;
    wallet->address = address;

    return wallet;
}

void zcash_release_wallet(struct zcash_wallet *wallet)
{
    /* The key and the address are owned by Rust, release them appropriately */
    zcash_release_address(wallet->address);
    zcash_release_key(wallet->key);
    free(wallet);
}

int zcash_export_key(uint8_t **raw_key, size_t *raw_key_length, const struct zcash_key *key)
{
    /* Whatever gets exported must be allocated with C's allocator, as the caller
     * is expected to just call "free" */
    *raw_key_length = 32;
    *raw_key = malloc(sizeof(uint8_t) * *raw_key_length);
    if (!*raw_key)
        return -ENOMEM;
    zcash_export_key_impl(*raw_key, key);
    return 0;
}

int zcash_export_address(uint8_t **raw_address, size_t *raw_address_length, const struct zcash_address *address)
{
    /* Whatever gets exported must be allocated with C's allocator, as the caller
     * is expected to just call "free" */
    *raw_address_length = 43;
    *raw_address = malloc(sizeof(uint8_t) * *raw_address_length);
    if (!*raw_address)
        return -ENOMEM;
    zcash_export_address_impl(*raw_address, address);
    return 0;
}

int zcash_export_advice(uint8_t **raw_advice, size_t *raw_advice_length, const struct zcash_advice *advice)
{
    /* Whatever gets exported must be allocated with C's allocator, as the caller
     * is expected to just call "free" */
    *raw_advice_length = 128;
    *raw_advice = malloc(sizeof(uint8_t) * *raw_advice_length);
    if (!*raw_advice)
        return -ENOMEM;
    zcash_export_advice_impl(*raw_advice, advice);
    return 0;
}

int zcash_export_transaction(uint8_t **raw_transaction, size_t *raw_transaction_length, const struct zcash_transaction *transaction)
{
    *raw_transaction_length = zcash_get_raw_transaction_length(transaction);
    *raw_transaction = malloc(sizeof(uint8_t) * *raw_transaction_length);
    if (!*raw_transaction)
        return -ENOMEM;
    zcash_export_transaction_impl(*raw_transaction, transaction);
    return 0;
}

int zcash_export_signature(uint8_t **raw_signature, size_t *raw_signature_length, const struct zcash_authorization *signature)
{
    *raw_signature_length = 64;
    *raw_signature = malloc(sizeof(uint8_t) * *raw_signature_length);
    if (!*raw_signature)
        return -ENOMEM;
    zcash_export_signature_impl(*raw_signature, signature);
    return 0;
}
