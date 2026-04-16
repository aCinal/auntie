#include "functionality.h"
#include "printf.h"
#include <sgx_trts.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int evaluate_functionality(
    uint8_t *outputs[AUNTIE_NUM_PLAYERS],
    size_t output_lengths[AUNTIE_NUM_PLAYERS],
    zatoshis_t payouts[AUNTIE_NUM_PLAYERS],
    uint8_t *const inputs[AUNTIE_NUM_PLAYERS],
    const size_t input_lengths[AUNTIE_NUM_PLAYERS],
    const zatoshis_t deposits[AUNTIE_NUM_PLAYERS]
)
{
#define WINNER_MESSAGE  "YOU WIN"
#define LOSER_MESSAGE   "YOU LOSE"

    sgx_status_t status;
    uint32_t winner;
    zatoshis_t sum_deposit;
    int ret;

    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++)
        outputs[i] = NULL;

    /* Use a lottery as a simple but representative functionality */

    /* Choose the winner */
    status = sgx_read_rand((unsigned char *) &winner, sizeof(winner));
    if (status != SGX_SUCCESS) {
        printf("%s: sgx_read_rand failed with error 0x%x\n", __func__, status);
        return -EFAULT;
    }
    winner = winner % AUNTIE_NUM_PLAYERS;

    sum_deposit = 0;
    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++)

    /* Initialize outputs of the losers */
    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++) {
        sum_deposit += deposits[i];

        if (i == winner)
            continue;

        payouts[i] = 0;
        outputs[i] = malloc(sizeof(LOSER_MESSAGE));
        if (!outputs[i]) {
            ret = -ENOMEM;
            goto error;
        }
        (void) memcpy(outputs[i], LOSER_MESSAGE, sizeof(LOSER_MESSAGE));
        output_lengths[i] = sizeof(LOSER_MESSAGE);
    }

    payouts[winner] = sum_deposit;
    outputs[winner] = malloc(sizeof(WINNER_MESSAGE));
    if (!outputs[winner]) {
        ret = -ENOMEM;
        goto error;
    }
    (void) memcpy(outputs[winner], WINNER_MESSAGE, sizeof(WINNER_MESSAGE));
    output_lengths[winner] = sizeof(WINNER_MESSAGE);

    return 0;

error:
    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++)
        if (outputs[i])
            free(outputs[i]);
    return ret;
}
