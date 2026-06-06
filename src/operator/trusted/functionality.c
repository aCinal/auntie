#include "functionality.h"
#include "printf.h"
#include <sgx_trts.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static inline void conditional_move(void *out, const void *in, size_t length, int move)
{
    uint8_t *bout = out;
    const uint8_t *bin = in;
    uint8_t mask = (uint8_t) -move;
    uint8_t rmask = ~mask;

    for (size_t i = 0; i < length; i++)
        bout[i] = (bin[i] & mask) ^ (bout[i] & rmask);
}

int evaluate_functionality(
    uint8_t *outputs[AUNTIE_NUM_PLAYERS],
    size_t output_lengths[AUNTIE_NUM_PLAYERS],
    zat_t payouts[AUNTIE_NUM_PLAYERS],
    uint8_t *const inputs[AUNTIE_NUM_PLAYERS],
    const size_t input_lengths[AUNTIE_NUM_PLAYERS],
    const zat_t deposits[AUNTIE_NUM_PLAYERS]
)
{
    /* Note that care should be taken to not leak information about the contract's outcome
     * to the operator via side channels. Similarly, the lengths of the outputs can reveal
     * information about the outcome. */
#define WINNER_MESSAGE  "YOU WIN!"
#define LOSER_MESSAGE   "YOU LOSE"
    static_assert(sizeof(WINNER_MESSAGE) == sizeof(LOSER_MESSAGE));

    sgx_status_t status;
    uint32_t winner;
    zat_t sum_deposit;
    int ret;

    for (uint32_t i = 0; i < AUNTIE_NUM_PLAYERS; i++)
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
    for (uint32_t i = 0; i < AUNTIE_NUM_PLAYERS; i++)
        sum_deposit += deposits[i];

    for (uint32_t i = 0; i < AUNTIE_NUM_PLAYERS; i++) {
        payouts[i] = 0;
        outputs[i] = malloc(sizeof(LOSER_MESSAGE));
        if (!outputs[i]) {
            ret = -ENOMEM;
            goto error;
        }
        (void) memcpy(outputs[i], LOSER_MESSAGE, sizeof(LOSER_MESSAGE));
        output_lengths[i] = sizeof(LOSER_MESSAGE);
    }

    for (uint32_t i = 0; i < AUNTIE_NUM_PLAYERS; i++) {
        uint32_t move = i ^ winner;
        move |= (move >> 16);
        move |= (move >> 8);
        move |= (move >> 4);
        move |= (move >> 2);
        move |= (move >> 1);
        move  = (move ^ 1) & 1;
        /* move is 1 if i == winner and 0 otherwise */

        conditional_move(&payouts[i], &sum_deposit, sizeof(zat_t), move);
        conditional_move(outputs[i], WINNER_MESSAGE, sizeof(WINNER_MESSAGE), move);
    }

    return 0;

error:
    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++)
        if (outputs[i])
            free(outputs[i]);

    return ret;
}
