
#ifndef __SRC_OPERATOR_TRUSTED_MUTUAL_ATTESTATION_H
#define __SRC_OPERATOR_TRUSTED_MUTUAL_ATTESTATION_H

#include "players.h"
#include <sgx_report.h>

struct player *mutual_attestation(void *context, sgx_target_info_t *quoting_enclave);

#endif /* __SRC_OPERATOR_TRUSTED_MUTUAL_ATTESTATION_H */
