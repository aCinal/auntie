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

int ocall_get_deposits_callback(void *context, const uint8_t *deposit_transactions, size_t deposit_transactions_length)
{
    FILE *file;
    size_t bytes_written;

    file = context;
    bytes_written = fwrite(deposit_transactions, sizeof(uint8_t), deposit_transactions_length, file);
    if (bytes_written != deposit_transactions_length) {
        printf("%s: fwrite failed to write %lu byte(s) and returned %lu\n", __func__, deposit_transactions_length, bytes_written);
        return -EIO;
    }

    return 0;
}

int ocall_settle_callback(void *context, const uint8_t *payout_key, size_t payout_key_length, const uint8_t *output, size_t output_length)
{
    FILE *file;
    size_t bytes_written;

    file = context;
    bytes_written = fwrite(payout_key, sizeof(uint8_t), payout_key_length, file);
    if (bytes_written != payout_key_length) {
        printf("%s: fwrite failed to write %lu byte(s) and returned %lu\n", __func__, payout_key_length, bytes_written);
        return -EIO;
    }

    // TODO: Separate the payout key from the output nicely

    bytes_written = fwrite(output, sizeof(uint8_t), output_length, file);
    if (bytes_written != output_length) {
        printf("%s: fwrite failed to write %lu byte(s) and returned %lu\n", __func__, output_length, bytes_written);
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
