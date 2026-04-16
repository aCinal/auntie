

#ifndef __SRC_COMMON_TRUSTED_CORE_COMMON_H
#define __SRC_COMMON_TRUSTED_CORE_COMMON_H

struct auntie_msg_deposit_and_input {
    zatoshis_t deposit_amount;
    uint32_t deposit_transaction_offset;
    uint32_t input_offset;
    uint32_t payout_address_offset;
    uint8_t data[0];
};

struct auntie_msg_clear_contract {
    uint32_t settlement_transaction_offset;
    uint32_t output_offset;
    uint32_t deposit_transactions_offsets[AUNTIE_NUM_PLAYERS + 1];
    uint8_t data[0];
};

struct auntie_msg_confirm_deposits {
    uint32_t signature_length;
    uint8_t signature[0];
};

#endif /* __SRC_COMMON_TRUSTED_CORE_COMMON_H */
