#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>

int ocall_initialize_callback(void *context, const uint8_t *deposit_address, size_t deposit_address_length)
{
    FILE *file;
    size_t bytes_written;

    file = context;
    bytes_written = fwrite(deposit_address, sizeof(uint8_t), deposit_address_length, file);
    if (bytes_written != deposit_address_length) {
        printf("%s: fwrite failed to write %lu byte(s) and returned %lu\n", __func__, deposit_address_length, bytes_written);
        return -EIO;
    }

    return 0;
}

int ocall_clear_contract_callback(void *context, const uint8_t *deposit_transaction, size_t deposit_transaction_length)
{
    FILE *file;
    size_t bytes_written;

    file = context;
    bytes_written = fwrite(deposit_transaction, sizeof(uint8_t), deposit_transaction_length, file);
    if (bytes_written != deposit_transaction_length) {
        printf("%s: fwrite failed to write %lu byte(s) and returned %lu\n", __func__, deposit_transaction_length, bytes_written);
        return -EIO;
    }

    return 0;
}

int ocall_finalize_callback(void *context, const uint8_t *settlement_transaction, size_t settlement_transaction_length)
{
    FILE *file;
    size_t bytes_written;

    file = context;
    bytes_written = fwrite(settlement_transaction, sizeof(uint8_t), settlement_transaction_length, file);
    if (bytes_written != settlement_transaction_length) {
        printf("%s: fwrite failed to write %lu byte(s) and returned %lu\n", __func__, settlement_transaction_length, bytes_written);
        return -EIO;
    }

    return 0;
}

int ocall_refund_callback(void *context, const uint8_t *deposit_key, size_t deposit_key_length)
{
    FILE *file;
    size_t bytes_written;

    file = context;
    bytes_written = fwrite(deposit_key, sizeof(uint8_t), deposit_key_length, file);
    if (bytes_written != deposit_key_length) {
        printf("%s: fwrite failed to write %lu byte(s) and returned %lu\n", __func__, deposit_key_length, bytes_written);
        return -EIO;
    }

    return 0;
}
