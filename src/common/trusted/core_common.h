
#ifndef __SRC_COMMON_TRUSTED_CORE_COMMON_H
#define __SRC_COMMON_TRUSTED_CORE_COMMON_H

struct auntie_msg_deposit_and_input {
    /* Note that we do not need to send the deposit amount explicitly as done in the paper since
     * we send the full viewing key anyway for the purpose of zk-SNARK generation. The operator's
     * TEE can thus find the deposit amount on its own without the help of the player's TEE. */
    zat_t deposit_amount;
    uint32_t deposit_transaction_offset;
    uint32_t input_offset;
    uint32_t payout_address_offset;
    uint32_t advice_offset;
    uint8_t data[0];
};

struct auntie_msg_clear_contract {
    uint8_t settlement_sighash[32];
    uint8_t settlement_txid[32];
    uint32_t output_offset;
    uint32_t deposit_transactions_offsets[AUNTIE_NUM_PLAYERS + 1];
    uint8_t data[0];
};

struct auntie_msg_confirm_deposits {
    uint32_t signature_length;
    uint8_t signature[0];
};

#endif /* __SRC_COMMON_TRUSTED_CORE_COMMON_H */
