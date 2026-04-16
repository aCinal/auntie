
#ifndef __SRC_COMMON_TRUSTED_OCALL_H
#define __SRC_COMMON_TRUSTED_OCALL_H

#include <sgx_trts.h>
#include "printf.h"
#include "trusted_glue_code.h"

#define unlikely(x)  __builtin_expect(!!(x), 0)

#define OCALL(__ocall_name, ...) ({ \
    int ocall_ret; \
    sgx_status_t status = __ocall_name(&ocall_ret, ##__VA_ARGS__); \
    if (unlikely(status != SGX_SUCCESS)) { \
        printf("%s: failed to issue OCALL %s with error 0x%x\n", \
            __func__, #__ocall_name, status); \
        abort(); \
    } \
    ocall_ret; \
})

#endif /* __SRC_COMMON_TRUSTED_OCALL_H */
