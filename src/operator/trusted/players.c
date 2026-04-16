#include "players.h"
#include "messages.h"
#include "mutual_attestation_common.h"
#include "ocall.h"
#include <assert.h>
#include <errno.h>
#include <string.h>

struct player *players[AUNTIE_NUM_PLAYERS];
static int players_connected;

int player_id_ok(uint32_t id)
{
    return id && id <= AUNTIE_NUM_PLAYERS && !players[id - 1];
}

void add_player(struct player *player)
{
    uint32_t id = player->id;
    assert(player_id_ok(id));
    players[id - 1] = player;
    players_connected++;
}

int all_players_connected(void)
{
    return players_connected == AUNTIE_NUM_PLAYERS;
}

int present_quotes(void)
{
    struct auntie_msg *msg;
    struct auntie_msg_players_quotes_broadcast *payload;
    uint32_t total_quotes_length;
    uint32_t offset;
    int ret;

    printf("%s: all quotes received, outputting for verification\n", __func__);

    total_quotes_length = 0;
    /* Show the quotes to the host */
    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++) {

        ret = OCALL(ocall_print_quote, players[i]->quote, players[i]->quote_length, players[i]->report_data);
        if (ret) {
            printf("%s: failed to print quote of player %d with error %d\n", __func__, i + 1, ret);
            return ret;
        }

        total_quotes_length += players[i]->quote_length;
    }

    printf("%s: broadcasting the quotes to all players\n", __func__);
    /* Create a broadcast message */
    msg = auntie_msg_create(AUNTIE_MSG_PLAYERS_QUOTES_BROADCAST, sizeof(*payload) + total_quotes_length);
    if (!msg) {
        printf("%s: failed to create players' quotes broadcast message of size %u\n", \
            __func__, sizeof(*payload) + total_quotes_length);
        return -ENOMEM;
    }
    payload = auntie_msg_get_payload(msg);

    /* Collect all report data */
    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++)
        payload->report_data[i] = players[i]->report_data;
    /* Collect the quotes */
    offset = 0;
    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++) {
        payload->quotes_offsets[i] = offset;
        (void) memcpy(payload->quotes + offset, players[i]->quote, players[i]->quote_length);
        offset += players[i]->quote_length;
    }

    /* Send the message to every player */
    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++) {

        ret = auntie_msg_send(msg, players[i]->channel);
        if (ret) {
            printf("%s: failed to send players' quotes broadcast message on channel %u\n", \
                __func__, channel_id(players[i]->channel));
            auntie_msg_destroy(msg);
            return ret;
        }
    }
    auntie_msg_destroy(msg);

    /* Receive an acknowledgement from each player */
    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++) {

        printf("%s: waiting for acknowledgement on channel %u\n", __func__, channel_id(players[i]->channel));
        msg = auntie_msg_receive(players[i]->channel);
        if (!msg) {
            printf("%s: failed to receive acknowledgement of quotes on channel %u\n", \
                __func__, channel_id(players[i]->channel));
            return -EIO;
        }

        if (auntie_msg_get_type(msg) != AUNTIE_MSG_PLAYERS_QUOTES_OK) {
            printf("%s: unexpected message %u received (expected: %u)\n", \
                __func__, auntie_msg_get_type(msg), AUNTIE_MSG_PLAYERS_QUOTES_OK);
            auntie_msg_destroy(msg);
            return -EIO;
        }

        auntie_msg_destroy(msg);
    }

    printf("%s: all acknowledgements received, broadcasting final notification that contract is live\n", __func__);

    /* All acknowledgements received, broadcast one final notification that the contract is live */
    msg = auntie_msg_create(AUNTIE_MSG_SETUP_COMPLETE, 0);
    if (!msg) {
        printf("%s: failed to create setup complete notification message\n", __func__);
        return -ENOMEM;
    }

    /* Send the message to every player */
    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++) {

        ret = auntie_msg_send(msg, players[i]->channel);
        if (ret) {
            printf("%s: failed to send setup complete notification message on channel %u\n", \
                __func__, channel_id(players[i]->channel));
            auntie_msg_destroy(msg);
            return ret;
        }
    }
    auntie_msg_destroy(msg);

    return 0;
}
