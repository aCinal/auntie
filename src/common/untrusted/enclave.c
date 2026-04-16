#include "ecall.h"
#include <stdio.h>
#include <errno.h>

sgx_enclave_id_t the_enclave;

int create_enclave(const char *soname, int debug)
{
    sgx_status_t status;
    sgx_launch_token_t launch_token = {};
    int updated = 0;

    status = sgx_create_enclave(soname, debug, &launch_token, &updated, &the_enclave, NULL);
    if (status != SGX_SUCCESS) {
        fprintf(stderr, "Failed to start enclave '%s' with error 0x%x\n", soname, status);
        return -ENODEV;
    }

    return 0;
}
