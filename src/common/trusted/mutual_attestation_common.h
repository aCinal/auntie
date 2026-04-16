
#ifndef __SRC_COMMON_TRUSTED_MUTUAL_ATTESTATION_COMMON_H
#define __SRC_COMMON_TRUSTED_MUTUAL_ATTESTATION_COMMON_H

#include "messages.h"
#include <sgx_report.h>
#include <sgx_tcrypto.h>

#define ATTESTATION_NONCE_SIZE  ((uint32_t) 32)

/* Each player needs to run remote attestation against every other player's TEE
 * as well as against the operator's TEE. The problem is that player i's verification
 * of player j's TEE must be tied to the _channel_ established between player j's TEE
 * and the _operator's_ TEE (otherwise this attestation is meaningless). We solve it
 * the following way: player i's operator runs _mutual_ remote attestation protocol
 * against the operator's TEE, and the operator's TEE caches the quotes obtained from
 * every player's TEE. The quotes are then distributed through trusted channels to all
 * players for them to verify them.
 *
 * The protocol for mutual remote attestation is as follows:
 *
 *
 *   Player(i).TEE                                                                     Operator.TEE
 *
 *   a <- ZZ_q
 *   Ga <- g^a
 *   nonce <- {0,1}^256                          Ga, nonce, i
 *                       ------------------------------------------------------------->
 *                                                                                     b <- ZZ_q
 *                                                                                     Gb <- g^b
 *                                                                                     K <- KDF(Ga^b)
 *                                                                                     nonce' <- {0,1}^256
 *                                                                                     report <- EREPORT(SHA-256(nonce, Ga, Gb))
 *                                             Gb, nonce', quote                       quote <- QE(report)
 *                       <-------------------------------------------------------------
 *   K <- KDF(Gb^a)
 *   report' <- EREPORT(SHA-256(nonce', Ga, Gb))
 *   quote' <- QE(report')                          quote'
 *                        ------------------------------------------------------------>
 */

struct auntie_msg_attestation_syn {
    sgx_ec256_public_t ga;
    uint32_t id;
    uint8_t nonce[ATTESTATION_NONCE_SIZE];
};

struct auntie_msg_attestation_synack {
    sgx_ec256_public_t gb;
    uint8_t nonce[ATTESTATION_NONCE_SIZE];
    uint8_t quote[0];
};

struct auntie_msg_attestation_ack {
    uint8_t quote[0];
};

struct auntie_msg_players_quotes_broadcast {
    sgx_report_data_t report_data[AUNTIE_NUM_PLAYERS];
    uint32_t quotes_offsets[AUNTIE_NUM_PLAYERS];
    uint8_t quotes[0];
};

int get_report_data(sgx_report_data_t *report_data, const uint8_t nonce[ATTESTATION_NONCE_SIZE], const sgx_ec256_public_t *ga, const sgx_ec256_public_t *gb);
int get_session_key(sgx_key_128bit_t *session_key, const sgx_ec256_public_t *ga, const sgx_ec256_public_t *gb, const sgx_ec256_dh_shared_t *gab);

#endif /* __SRC_COMMON_TRUSTED_MUTUAL_ATTESTATION_COMMON_H */
