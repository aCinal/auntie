
#ifndef __SRC_COMMON_UNTRUSTED_ECALL_H
#define __SRC_COMMON_UNTRUSTED_ECALL_H

#include <sgx_urts.h>
#include <stdlib.h>
#include <stdio.h>
#include "untrusted_glue_code.h"

extern sgx_enclave_id_t the_enclave;

int create_enclave(const char *soname, int debug);

#define unlikely(x)  __builtin_expect(!!(x), 0)

#define ECALL(__ecall_name, ...) ({ \
    int ecall_ret; \
    sgx_status_t status = __ecall_name(the_enclave, &ecall_ret, ##__VA_ARGS__); \
    if (unlikely(status != SGX_SUCCESS)) { \
        fprintf(stderr, "%s: failed to issue ECALL %s with error 0x%x\n", \
            __func__, #__ecall_name, status); \
        exit(EXIT_FAILURE); \
    } \
    ecall_ret; \
})

#endif /* __SRC_COMMON_UNTRUSTED_ECALL_H */
