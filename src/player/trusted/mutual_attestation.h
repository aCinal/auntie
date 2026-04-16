
#ifndef __SRC_PLAYER_TRUSTED_MUTUAL_ATTESTATION_H
#define __SRC_PLAYER_TRUSTED_MUTUAL_ATTESTATION_H

#include "channel.h"
#include <sgx_report.h>

struct operator {
    struct channel *channel;
    uint8_t *quote;
    uint32_t quote_length;
    sgx_report_data_t report_data;
};

struct operator *mutual_attestation(void *context, sgx_target_info_t *quoting_enclave, uint32_t id);

#endif /* __SRC_PLAYER_TRUSTED_MUTUAL_ATTESTATION_H */
