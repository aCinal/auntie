
#ifndef __SRC_OPERATOR_TRUSTED_PLAYERS_H
#define __SRC_OPERATOR_TRUSTED_PLAYERS_H

#include "channel.h"
#include "zcash.h"
#include <sgx_report.h>
#include <stdint.h>

struct player {
    struct channel *channel;
    uint32_t id;
    sgx_report_data_t report_data;
    uint8_t *quote;
    uint32_t quote_length;
};

extern struct player *players[AUNTIE_NUM_PLAYERS];

int player_id_ok(uint32_t id);
void add_player(struct player *player);
int all_players_connected(void);
int present_quotes(void);

#endif /* __SRC_OPERATOR_TRUSTED_PLAYERS_H */
