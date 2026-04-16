#include "messages.h"
#include "printf.h"
#include <stdlib.h>

struct auntie_msg_hdr {
    uint32_t msg_type;
    uint32_t payload_size;
};

struct auntie_msg {
    struct auntie_msg_hdr header;
    uint8_t payload[0];
};

struct auntie_msg *auntie_msg_create(uint32_t msg_type, uint32_t payload_size)
{
    struct auntie_msg *msg = malloc(sizeof(struct auntie_msg) + payload_size);
    if (msg) {
        msg->header.msg_type = msg_type;
        msg->header.payload_size = payload_size;
    }
    return msg;
}

void *auntie_msg_get_payload(struct auntie_msg *msg)
{
    return msg->payload;
}

uint32_t auntie_msg_get_payload_size(struct auntie_msg *msg)
{
    return msg->header.payload_size;
}

uint32_t auntie_msg_get_type(struct auntie_msg *msg)
{
    return msg->header.msg_type;
}

void auntie_msg_destroy(struct auntie_msg *msg)
{
    free(msg);
}

int auntie_msg_send(struct auntie_msg *msg, struct channel *channel)
{
    int ret;
    ret = channel_egress(channel, (void *) msg, sizeof(msg) + msg->header.payload_size);
    return ret < 0 ? ret : 0;
}

struct auntie_msg *auntie_msg_receive(struct channel *channel)
{
    int ret;
    struct auntie_msg_hdr hdr;
    struct auntie_msg *msg;
    size_t bytes_received;

    /* Receive the header first */
    ret = channel_ingress(channel, (uint8_t *) &hdr, sizeof(hdr));
    if (ret != sizeof(hdr)) {
        printf("%s: failed to receive message header on channel %u (channel_ingress returned %d)\n", \
            __func__, channel_id(channel), ret);
        return NULL;
    }
    /* Verify message type */
    if (hdr.msg_type >= __AUNTIE_MSG_TYPE_COUNT) {
        printf("%s: invalid message type %u on channel %u\n", \
            __func__, hdr.msg_type, channel_id(channel));
        return NULL;
    }
    /* Allocate buffer for the payload */
    msg = malloc(sizeof(*msg) + hdr.payload_size);
    if (!msg) {
        printf("%s: failed to allocate buffer of size %lu for message %u on channel %u\n", \
            __func__, sizeof(*msg) + hdr.payload_size, hdr.msg_type, channel_id(channel));
        return NULL;
    }

    bytes_received = 0;
    /* Receive the payload */
    while (bytes_received < hdr.payload_size) {

        ret = channel_ingress(channel, msg->payload + bytes_received, hdr.payload_size - bytes_received);
        if (ret < 0) {
            printf("%s: channel_ingress returned %d for channel %u and message %u (received %u out of %u byte(s) of the payload)\n", \
                __func__, ret, channel_id(channel), hdr.msg_type, bytes_received, hdr.payload_size);
            free(msg);
            return NULL;
        }

        bytes_received += ret;
    }

    msg->header = hdr;
    return msg;
}
