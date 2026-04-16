
#ifndef __SRC_COMMON_TRUSTED_CHANNEL_H
#define __SRC_COMMON_TRUSTED_CHANNEL_H

#include <stdint.h>
#include <sgx_tcrypto.h>

struct channel;
extern struct channel *channels;

struct channel *channel_create(void *connection);
void channel_encrypt(struct channel *channel, const sgx_key_128bit_t *session_key);
void channel_assign_id(struct channel *channel, uint32_t id);
void channel_teardown(struct channel *channel);
uint32_t channel_id(const struct channel *channel);
int channel_ingress(struct channel *channel, uint8_t *buffer, size_t size);
int channel_egress(struct channel *channel, const uint8_t *buffer, size_t size);

#endif /* __SRC_COMMON_TRUSTED_CHANNEL_H */
