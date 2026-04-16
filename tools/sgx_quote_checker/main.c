#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sgx_dcap_quoteverify.h>
#include <sgx_report.h>

static inline void write_hex_buffer(FILE *stream, const uint8_t *buffer, size_t len)
{
    for (size_t i = 0; i < len; i++)
        fprintf(stream, "%02x", buffer[i]);
}

static size_t verify_quote_authenticity(const uint8_t *quote, size_t len)
{
    quote3_error_t ret;
    uint32_t collateral_expired;
    sgx_ql_qv_result_t result;

    ret = sgx_qv_verify_quote(
        quote,
        len,
        NULL,
        time(NULL),
        &collateral_expired,
        &result,
        NULL,
        0,
        NULL
    );
    if (ret != SGX_QL_SUCCESS) {
        fprintf(stderr, "%s: sgx_qv_verify_quote returned error 0x%x\n", \
            __func__, ret);
        return EFAULT;
    }

    if (result != SGX_QL_QV_RESULT_OK) {
        fprintf(stderr, "%s: quote invalid, result=0x%x\n", \
            __func__, result);
        return EINVAL;
    }

    // TODO: Check collateral expiration status?

    return 0;
}

static inline size_t check_report_correct(const uint8_t *quote, const char mrenclave[32], const char report_data[64])
{
    sgx_quote3_t *quote3;

    quote3 = (sgx_quote3_t *) quote;
    if (memcmp(mrenclave, &quote3->report_body.mr_enclave, 32)) {
        fprintf(stderr, "%s: MRENCLAVE invalid in the quote (expected: ", __func__);
        write_hex_buffer(stderr, mrenclave, 32);
        fprintf(stderr, ", actual: ");
        write_hex_buffer(stderr, (const uint8_t *) &quote3->report_body.mr_enclave, 32);
        fprintf(stderr, ")\n");
        return EINVAL;
    }

    if (memcmp(report_data, &quote3->report_body.report_data, 64)) {
        fprintf(stderr, "%s: report data invalid in the quote (expected: ", __func__);
        write_hex_buffer(stderr, report_data, 64);
        fprintf(stderr, ", actual: ");
        write_hex_buffer(stderr, (const uint8_t *) &quote3->report_body.report_data, 64);
        fprintf(stderr, ")\n");
        return EINVAL;
    }

    if (quote3->report_body.attributes.flags & SGX_FLAGS_DEBUG) {
        fprintf(stderr, "%s: enclave built in debug mode\n", __func__);
        return EINVAL;
    }

    // TODO: Check other flags and attributes

    return 0;
}

int main(int argc, char *argv[])
{
    uint8_t mrenclave[32];
    uint8_t report_data[64];
    uint8_t *quote;
    uint32_t quote_length;
    size_t ret;

    (void) setvbuf(stdout, NULL, _IONBF, 0);
    (void) setvbuf(stderr, NULL, _IONBF, 0);

    for (; /* ever */ ;) {

        /* Read on stdin:
         *   0x00-0x1f expected MRENCLAVE,
         *   0x20-0x5f expected report data
         *   0x60-0x63 quote length (little endian)
         *             quote
         */

        ret = fread(mrenclave, sizeof(mrenclave), 1, stdin);
        if (ret != 1) {
            fprintf(stderr, "fread() failed to read in MRENCLAVE: %zu\n", ret);
            exit(EXIT_FAILURE);
        }

        ret = fread(report_data, sizeof(report_data), 1, stdin);
        if (ret != 1) {
            fprintf(stderr, "fread() failed to read in report data: %zu\n", ret);
            exit(EXIT_FAILURE);
        }

        ret = fread(&quote_length, sizeof(quote_length), 1, stdin);
        if (ret != 1) {
            fprintf(stderr, "fread() failed to read in quote length: %zu\n", ret);
            exit(EXIT_FAILURE);
        }

        quote = malloc(quote_length);
        if (!quote) {
            fprintf(stderr, "Failed to allocate %u byte(s) for the quote\n", quote_length);
            exit(EXIT_FAILURE);
        }

        ret = fread(quote, sizeof(uint8_t), quote_length, stdin);
        if (ret != quote_length) {
            fprintf(stderr, "fread() failed to read in quote of length %u: %zu\n", quote_length, ret);
            exit(EXIT_FAILURE);
        }

        ret = verify_quote_authenticity(quote, quote_length);
        if (ret) {
            fprintf(stderr, "Quote authenticity check failed with error %zu\n", ret);
            exit(EXIT_FAILURE);
        }

        ret = check_report_correct(quote, mrenclave, report_data);
        if (ret) {
            fprintf(stderr, "Report correctness check failed with error %zu\n", ret);
            exit(EXIT_FAILURE);
        }

        free(quote);
        fprintf(stdout, "Quote ok\n");
    }

    return 0;
}
