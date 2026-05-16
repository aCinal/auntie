#include "mutual_attestation_common.h"
#include "printf.h"
#include <string.h>
#include <errno.h>

int get_report_data(sgx_report_data_t *report_data, const uint8_t nonce[ATTESTATION_NONCE_SIZE], const sgx_ec256_public_t *ga, const sgx_ec256_public_t *gb)
{
    sgx_sha_state_handle_t sha;

    /* Bind the Diffie-Hellman exchange and the nonce to the hardware report */
    (void) memset(report_data, 0, sizeof(*report_data));
    /* Note that REPORTDATA is 64 bytes: use only the leading 32 bytes and zero the rest */
    if (SGX_SUCCESS != sgx_sha256_init(&sha))
        return -ENOMEM;
    (void) sgx_sha256_update(nonce, ATTESTATION_NONCE_SIZE, sha);
    (void) sgx_sha256_update((const uint8_t *) ga, sizeof(*ga), sha);
    (void) sgx_sha256_update((const uint8_t *) gb, sizeof(*gb), sha);
    (void) sgx_sha256_get_hash(sha, (sgx_sha256_hash_t *) report_data);
    (void) sgx_sha256_close(sha);
    return 0;
}

int get_session_key(sgx_key_128bit_t *session_key, const sgx_ec256_public_t *ga, const sgx_ec256_public_t *gb, const sgx_ec256_dh_shared_t *gab)
{
    sgx_cmac_state_handle_t cmac;
    sgx_cmac_128bit_key_t cmac_zero_key;
    sgx_cmac_128bit_key_t cmac_derivation_key;
    sgx_status_t status;
    /* Define a 16-byte domain separator */
    static const uint8_t domain_separator[] = "auntiecontract\x01";

    (void) memset(cmac_zero_key, 0, sizeof(cmac_zero_key));
    status = sgx_rijndael128_cmac_msg(
        &cmac_zero_key,
        (const uint8_t *) gab,
        sizeof(*gab),
        (sgx_cmac_128bit_tag_t *) &cmac_derivation_key
    );
    if (status != SGX_SUCCESS) {
        printf("%s: sgx_rijndael128_cmac_msg failed with error 0x%x\n", __func__, status);
        return -EFAULT;
    }
    /* CMAC_k(domain_separator | g^a | g^b) with key k = CMAC_0(g^{ab}) */
    if (SGX_SUCCESS != sgx_cmac128_init(&cmac_derivation_key, &cmac))
        return -ENOMEM;
    (void) sgx_cmac128_update(domain_separator, sizeof(domain_separator), cmac);
    (void) sgx_cmac128_update((const uint8_t *) ga, sizeof(*ga), cmac);
    (void) sgx_cmac128_update((const uint8_t *) gb, sizeof(*gb), cmac);
    (void) sgx_cmac128_final(cmac, (sgx_cmac_128bit_tag_t *) session_key);
    (void) sgx_cmac128_close(cmac);

    return 0;
}
