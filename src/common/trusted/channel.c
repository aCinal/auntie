#include "channel.h"
#include "ocall.h"
#include <string.h>
#include <errno.h>

struct envelope {
    uint32_t length;
    uint8_t nonce[SGX_AESGCM_IV_SIZE];
    sgx_aes_gcm_128bit_tag_t tag;
    uint8_t ciphertext[0];
};

struct channel {
    sgx_key_128bit_t session_key;
    void *connection;
    int (*ingress)(struct channel *channel, uint8_t *buffer, size_t size);
    int (*egress)(struct channel *channel, const uint8_t *buffer, size_t size);
    uint32_t id;
    uint64_t ingress_sequence;
    uint64_t egress_sequence;
    uint32_t message_length;
    uint8_t *message;
    uint8_t *buffer;
};

static int plaintext_ingress(struct channel *channel, uint8_t *buffer, size_t size)
{
    return OCALL(ocall_ingress, channel->connection, buffer, size);
}

static int plaintext_egress(struct channel *channel, const uint8_t *buffer, size_t size)
{
    return OCALL(ocall_egress, channel->connection, buffer, size);
}

static int encrypted_ingress(struct channel *channel, uint8_t *buffer, size_t size)
{
    sgx_status_t status;
    int ret;
    size_t bytes_moved;
    struct envelope envelope;

    if (!channel->message) {
        /* Read in the header */
        ret = OCALL(ocall_ingress, channel->connection, (uint8_t *) &envelope, sizeof(envelope));
        if (ret != (int) sizeof(envelope)) {
            /* On error, return errno; if read too short, return -EIO; on 0, return -ECONNRESET */
            return ret < 0 ? ret : (ret ? -EIO : -ECONNRESET);
        }
        /* Allocate a buffer for the message */
        channel->buffer = malloc(envelope.length);
        if (!channel->buffer) {
            printf("%s: failed to allocate buffer for payload of size %u\n", __func__, envelope.length);
            return -ENOMEM;
        }
        channel->message = channel->buffer;
        channel->message_length = 0;
        /* Read in the payload */
        while (channel->message_length < envelope.length) {
            ret = OCALL(ocall_ingress, channel->connection, channel->message + channel->message_length, envelope.length - channel->message_length);
            if (ret < 0) {
                printf("%s: ocall_ingress failed with error %d\n", __func__, ret);
                return ret;
            }

            if (ret == 0)
                return -ECONNRESET;

            channel->message_length += ret;
        }

        /* The whole payload is now available, decrypt in place */
        status = sgx_aes_gcm_decrypt(
            channel->session_key,
            16,
            channel->message,
            channel->message_length,
            channel->message,
            envelope.nonce,
            SGX_AESGCM_IV_SIZE,
            (const uint8_t*) &channel->ingress_sequence,
            sizeof(channel->ingress_sequence),
            &envelope.tag
        );
        if (status != SGX_SUCCESS) {
            printf("%s: sgx_aes_gcm_decrypt returned 0x%x\n", __func__, status);
            return -EFAULT;
        }

        /* Update the sequence number */
        channel->ingress_sequence += envelope.length;
    }

    /* Copy from the present record */
    bytes_moved = (channel->message_length > size) ? size : channel->message_length;
    (void) memcpy(buffer, channel->message, bytes_moved);
    channel->message += bytes_moved;
    channel->message_length -= bytes_moved;

    if (!channel->message_length) {
        free(channel->buffer);
        channel->message = NULL;
    }

    return (int) bytes_moved;
}

static int encrypted_egress(struct channel *channel, const uint8_t *buffer, size_t size)
{
    sgx_status_t status;
    uint8_t *xmit_buffer;
    size_t xmit_buffer_size = size + sizeof(struct envelope);
    int ret;
    struct envelope *envelope;

    /* Allocate a transmitting buffer */
    xmit_buffer = malloc(xmit_buffer_size);
    if (!xmit_buffer)
        return -ENOMEM;

    envelope = (struct envelope *) xmit_buffer;
    envelope->length = size;
    /* Encrypt and authenticate the message */
    status = sgx_aes_gcm_encrypt(
        channel->session_key,
        16,
        buffer,
        size,
        envelope->ciphertext,
        envelope->nonce,
        SGX_AESGCM_IV_SIZE,
        (const uint8_t *) &channel->egress_sequence,
        sizeof(channel->egress_sequence),
        &envelope->tag
    );
    if (status != SGX_SUCCESS) {
        free(xmit_buffer);
        return -EFAULT;
    }

    /* Transmit the ciphertext and immediately release the transmit buffer */
    ret = OCALL(ocall_egress, channel->connection, xmit_buffer, xmit_buffer_size);
    free(xmit_buffer);
    if (ret != xmit_buffer_size) {
        /* Failed to write out everything, so decryption will fail at peer */
        return -EIO;
    }

    /* Update the sequence number */
    channel->egress_sequence += size;

    return size;
}

struct channel *channel_create(void *connection)
{
    struct channel *channel;
    channel = malloc(sizeof(*channel));
    if (channel) {
        channel->connection = connection;
        channel->id = (uint32_t) -1;
        channel->ingress = plaintext_ingress;
        channel->egress = plaintext_egress;
        channel->ingress_sequence = 0;
        channel->egress_sequence = 0;
        channel->message = NULL;
        channel->message_length = 0;
    }
    return channel;
}

void channel_encrypt(struct channel *channel, const sgx_key_128bit_t *session_key)
{
    (void) memcpy(channel->session_key, session_key, sizeof(sgx_key_128bit_t));
    channel->ingress = encrypted_ingress;
    channel->egress = encrypted_egress;
}

void channel_assign_id(struct channel *channel, uint32_t id)
{
    channel->id = id;
}

void channel_teardown(struct channel *channel)
{
    /* Give the untrusted host a chance to shut down the connection gracefully */
    (void) OCALL(ocall_drop_connection, channel->connection);
    if (channel->message)
        free(channel->message);
    free(channel);
}

uint32_t channel_id(const struct channel *channel)
{
    return channel->id;
}

int channel_ingress(struct channel *channel, uint8_t *buffer, size_t size)
{
    return channel->ingress(channel, buffer, size);
}

int channel_egress(struct channel *channel, const uint8_t *buffer, size_t size)
{
    return channel->egress(channel, buffer, size);
}
