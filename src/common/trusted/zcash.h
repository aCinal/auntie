
#ifndef __SRC_COMMON_TRUSTED_ZCASH_H
#define __SRC_COMMON_TRUSTED_ZCASH_H

#include <stdint.h>
#include <stddef.h>

typedef uint64_t zatoshis_t;

struct zcash_key;
struct zcash_address;
struct zcash_wallet {
    struct zcash_key *key;
    struct zcash_address *address;
};
struct zcash_transaction;
struct zcash_authorization;
struct zcash_blocks;

/** Create a wallet */
struct zcash_wallet *zcash_create_wallet(void);
/** Destroy a wallet */
void zcash_destroy_wallet(struct zcash_wallet *wallet);
/** Encode and export a key */
int zcash_export_key(uint8_t **raw_key, size_t *raw_key_length, const struct zcash_key *key);

/** Decode and import an external address */
struct zcash_address *zcash_import_address(const uint8_t *raw_address, size_t raw_address_length);
/** Encode and export an address */
int zcash_export_address(uint8_t **raw_address, size_t *raw_address_length, const struct zcash_address *address);
/** Free struct zcash_address */
void zcash_release_address(struct zcash_address *address);

/** Decode and import a Zcash transaction */
struct zcash_transaction *zcash_import_transaction(const uint8_t *raw_transaction, size_t raw_transaction_length);
/** Encode and export a Zcash transaction */
int zcash_export_transaction(uint8_t **raw_transaction, size_t *raw_transaction_length, const struct zcash_transaction *transaction);
/** Free struct zcash_transaction */
void zcash_release_transaction(struct zcash_transaction *transaction);
/** Create an unauthorized Zcash transaction */
struct zcash_transaction *zcash_create_transaction(
    struct zcash_transaction *const inputs[AUNTIE_NUM_PLAYERS + 1],
    zatoshis_t payouts[AUNTIE_NUM_PLAYERS + 1],
    struct zcash_address *const payout_addresses[AUNTIE_NUM_PLAYERS + 1]
);

/** Get amount deposited by a transaction to a specified address */
zatoshis_t zcash_deposited_amount(const struct zcash_transaction *transaction, const struct zcash_key *key);

/** Produce a single authorization for a Zcash transaction corresponding to the sole input associated with the deposit key */
struct zcash_authorization *zcash_sign_transaction(const struct zcash_key *deposit_key, const struct zcash_transaction *unauthorized_transaction);
/** Decode and import a transaction authorization */
struct zcash_authorization *zcash_import_signature(const uint8_t *raw_signature, size_t raw_signature_length);
/** Encode and export a transaction authorization */
int zcash_export_signature(uint8_t **raw_signature, size_t *raw_signature_length, const struct zcash_authorization *signature);
/** Combine all authorizations to produce a transaction ready to be broadcast */
struct zcash_transaction *zcash_authorize_transaction(const struct zcash_transaction *unauthorized_transaction, struct zcash_authorization *const signatures[AUNTIE_NUM_PLAYERS + 1]);
/** Free struct zcash_authorization */
void zcash_release_signature(struct zcash_authorization *signature);

/** Decode and import a chain of blocks */
struct zcash_blocks *zcash_import_blocks(const uint8_t *blocks, size_t blocks_length);
/** Free a chain of blocks */
void zcash_release_blocks(struct zcash_blocks *blocks);
/** Verify a given transaction is buried sufficiently deep in a valid chain of blocks and has been authorized with the given key and return the burial depth */
int zcash_authorized_and_buried(const struct zcash_transaction *unauthorized_transaction, const struct zcash_key *deposit_key, const struct zcash_blocks *blocks);
/** Verify a given chain of blocks and return its length on top of the checkpoint block */
int zcash_blocks_since_checkpoint(const struct zcash_blocks *blocks);

#endif /* __SRC_COMMON_TRUSTED_ZCASH_H */
