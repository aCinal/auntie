

#ifndef __SRC_COMMON_TRUSTED_MESSAGES_H
#define __SRC_COMMON_TRUSTED_MESSAGES_H

#include "channel.h"
#include <stdint.h>

/* Message 0-2 are part of the mutual attestation protocol between
 * each player's TEE and the operator's TEE (see mutual_attestation.h) */
#define AUNTIE_MSG_ATTESTATION_SYN               ((uint32_t) 0)
#define AUNTIE_MSG_ATTESTATION_SYNACK            ((uint32_t) 1)
#define AUNTIE_MSG_ATTESTATION_ACK               ((uint32_t) 2)
/* Message 3 is broadcast to all players once everyone has run mutual attestation */
#define AUNTIE_MSG_PLAYERS_QUOTES_BROADCAST      ((uint32_t) 3)
/* Message 4 is sent from the player's TEE to the operator's TEE to indicate the quotes
 * broadcast in message 3 were all accepted by the player */
#define AUNTIE_MSG_PLAYERS_QUOTES_OK             ((uint32_t) 4)
/* Message 5 is broadcast by the operator's TEE to all players to indicate setup is complete */
#define AUNTIE_MSG_SETUP_COMPLETE                ((uint32_t) 5)
/* Message 6 is sent from the player's TEE to the operator's TEE and includes
 * the deposit transaction, the deposit amount, the player's input, and the player's
 * payment address A_i^pay */
#define AUNTIE_MSG_DEPOSIT_AND_INPUT             ((uint32_t) 6)
/* Message 7 is sent from the operator's TEE to the player's TEE and includes
 * the operator's collateral transaction, the settlement transaction to be signed,
 * and the player's output y_i */
#define AUNTIE_MSG_CLEAR_CONTRACT                ((uint32_t) 7)
/* Message 8 is sent from the player's TEE to the operator's TEE and includes
 * a confirmation bit and the player's TEE's signature on the settlement transaction */
#define AUNTIE_MSG_CONFIRM_DEPOSITS              ((uint32_t) 8)
/* Keep this last for bounds checking */
#define __AUNTIE_MSG_TYPE_COUNT                  ((uint32_t) 9)

struct auntie_msg;

struct auntie_msg *auntie_msg_create(uint32_t msg_type, uint32_t payload_size);
void *auntie_msg_get_payload(struct auntie_msg *msg);
uint32_t auntie_msg_get_payload_size(struct auntie_msg *msg);
uint32_t auntie_msg_get_type(struct auntie_msg *msg);
void auntie_msg_destroy(struct auntie_msg *msg);
int auntie_msg_send(struct auntie_msg *msg, struct channel *channel);
struct auntie_msg *auntie_msg_receive(struct channel *channel);

#endif /* __SRC_COMMON_TRUSTED_MESSAGES_H */
