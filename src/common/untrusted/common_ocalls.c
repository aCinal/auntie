#include "connection.h"
#include "ecall.h"
#include "pretty.h"
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stddef.h>
#include <sgx_dcap_ql_wrapper.h>

FILE *quotes_file;

void ocall_print(const char *str)
{
    /* Print enclave messages in a different colour */
    printf(GREEN("%s"), str);
    /* We let the enclave handle its newlines so we flush manually after resetting the colour of the terminal */
    fflush(stdout);
}

int ocall_ingress(void *context, uint8_t *buffer, size_t size)
{
    ssize_t ret;
    struct connection *connection = context;

    ret = recv(connection->fd, buffer, size, 0);
    return ret >= 0 ? ret : -errno;
}

int ocall_egress(void *context, const uint8_t *buffer, size_t size)
{
    ssize_t ret;
    struct connection *connection = context;

    ret = send(connection->fd, buffer, size, 0);
    return ret >= 0 ? ret : -errno;
}

int ocall_drop_connection(void *context)
{
    struct connection *connection = context;

    close(connection->fd);
    free(connection);

    return 0;
}

int ocall_get_quoting_enclave(sgx_target_info_t *qe)
{
    quote3_error_t quote3_error;

    quote3_error = sgx_qe_get_target_info(qe);
    if (quote3_error != SGX_QL_SUCCESS) {
        fprintf(stderr, "Failed to fetch the quoting enclave's target info with error 0x%x\n", quote3_error);
        return -EAGAIN;
    }

    /* Log information about the quoting enclave */
    printf("Quoting enclave identity:\n");
    printf("  MRENCLAVE = ");
    for (int i = 0; i < sizeof(qe->mr_enclave); i++)
        printf("%02x", qe->mr_enclave.m[i]);
    printf("\n");
    printf("  Attributes: FLAGS=0x%016lx, XFRM=0x%016lx\n",
           qe->attributes.flags, qe->attributes.xfrm);
    printf("  SVN = %d\n", qe->config_svn);
    printf("  MiscSelect = 0x%08x\n", qe->misc_select);
    printf("  ConfigID = ");
    for (int i = 0; i < sizeof(qe->config_id); i++)
        printf("%02x", qe->config_id[i]);
    printf("\n");

    return 0;
}

int ocall_quote_me(const sgx_report_t *report)
{
    uint8_t *quote;
    uint32_t quote_size;
    quote3_error_t quote3_error;
    int ret;

    /* Get quote size */
    quote3_error = sgx_qe_get_quote_size(&quote_size);
    if (quote3_error != SGX_QL_SUCCESS) {
        fprintf(stderr, "%s: failed to get quote size with error 0x%x\n",
                __func__, quote3_error);
        return -EAGAIN;
    }
    /* Allocate the buffer */
    quote = malloc(quote_size);
    if (!quote) {
        fprintf(stderr, "%s: failed to allocate buffer for quote of size %u\n",
                __func__, quote_size);
        return -ENOMEM;
    }
    /* Quote the report */
    quote3_error = sgx_qe_get_quote(report, quote_size, quote);
    if (quote3_error != SGX_QL_SUCCESS) {
        fprintf(stderr, "%s: failed to quote with error 0x%x\n",
                __func__, quote3_error);
        free(quote);
        return -EAGAIN;
    }

    /* Call back into the enclave with the quote */
    ret = ECALL(ecall_quote_me_callback, quote, quote_size);
    if (ret)
        fprintf(stderr, "%s: callback failed with error %d\n",
                __func__, ret);

    free(quote);

    return ret;
}

int ocall_print_quote(const uint8_t *quote, size_t quote_length, sgx_report_data_t report_data)
{
    int bytes_written;

    if (!quotes_file) {
        printf("%s: quotes file unitialized!\n", __func__);
        return -EFAULT;
    }

    /* Write the quotes as text */
    for (size_t i = 0; i < sizeof(report_data); i++) {
        bytes_written = fprintf(quotes_file, "%02x", ((uint8_t *) &report_data)[i]);
        if (bytes_written != 2) {
            fprintf(stderr, "%s: failed to write out report data", __func__);
            return -EIO;
        }
    }
    bytes_written = fprintf(quotes_file, ", ");
    if (bytes_written != 2) {
        fprintf(stderr, "%s: failed to write out separator", __func__);
        return -EIO;
    }
    for (size_t i = 0; i < quote_length; i++) {
        bytes_written = fprintf(quotes_file, "%02x", quote[i]);
        if (bytes_written != 2) {
            fprintf(stderr, "%s: failed to write out quote", __func__);
            return -EIO;
        }
    }
    bytes_written = fprintf(quotes_file, "\n");
    if (bytes_written != 1) {
        fprintf(stderr, "%s: failed to write out newline", __func__);
        return -EIO;
    }
    return 0;
}
