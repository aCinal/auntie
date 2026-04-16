# Auntie

Intel SGX-based proof-of-concept implementation of the **Auntie** contract execution engine over Zcash, designed as part of the paper *Auntie: Unobservable Contracts from Zerocash and Trusted Execution Environments* ([eprint:2025/1965](https://eprint.iacr.org/2025/1965)).

## Usage

Install Intel SGX components, including [the software development kit (SDK) together with the platform software (PSW)](https://github.com/intel/confidential-computing.sgx), the [remote attestation libraries (DCAP)](https://github.com/intel/confidential-computing.tee.dcap), and the [Provisioning Certificate Caching Service (PCCS)](https://github.com/intel/confidential-computing.tee.dcap.pccs).

Set the parameters `AUNTIE_NUM_PLAYERS`, `AUNTIE_OPERATOR_COLLATERAL`, `AUNTIE_REFUND_DELAY_BLOCKS`, and `AUNTIE_SETTLE_DELAY_BLOCKS` in the top-level `Makefile`. See [the paper](https://eprint.iacr.org/2025/1965) for reference. Modify the functionality as needed in `src/operator/trusted/functionality.c`:
```c
int evaluate_functionality(
    uint8_t *outputs[AUNTIE_NUM_PLAYERS],
    size_t output_lengths[AUNTIE_NUM_PLAYERS],
    zatoshis_t payouts[AUNTIE_NUM_PLAYERS],
    uint8_t *const inputs[AUNTIE_NUM_PLAYERS],
    const size_t input_lengths[AUNTIE_NUM_PLAYERS],
    const zatoshis_t deposits[AUNTIE_NUM_PLAYERS]
)
{
    /* Transform the deposit amounts (d_i in the paper) and inputs (x_i in the paper)
     * into payouts (p_i in the paper) and outputs (y_i in the paper). Use randomness
     * as needed. */

    uint32_t lottery_winner;
    zatoshis_t sum_deposit;

    /* Choose the lottery winner */
    sgx_read_rand(&lottery_winner, sizeof(lottery_winner));
    lottery_winner = lottery_winner % AUNTIE_NUM_PLAYERS;

    for (int i = 0; i < AUNTIE_NUM_PLAYERS; i++)
        sum_deposit += deposits[i];
    payouts[lottery_winner] = sum_deposit;

    ...

    return 0;
}
```
Finally, modify `checkpoint_block` in `src/common/trusted/zcash.c`.

Build and sign the TEEs:
```bash
cd tools
./keygen.sh
cd ..
make
```

Launch the operator node:
```bash
./auntie_operator 15000 op.quotes
```

For each player `$i`, launch the player node:
```bash
./auntie_player $i <operator-ip>:15000 p$i.quotes
```

Once you see `Welcome to the Auntie shell! Type '?' or 'help' for more information, or 'quit' to exit.`, the counterparties' quotes had been written to `op.quotes` or `p$i.quotes`, respectively, and the node has transitioned to a shell that accepts commands from the host. Before engaging with the contract via this shell, the host should verify the identities of the counterparties' TEEs:
```bash
cd tools
# For the operator
./check_quotes.py o ../op.quotes
# For, say, player 1
./check_quotes.py p ../p1.quotes
cd ..
```
The `check_quotes.py` script fetches the measurements of the relevant types of TEEs and verifies the quotes.

If all checks pass, the operator should call `initialize` to create a Zcash wallet and obtain the deposit address. Having made the deposit, they should input the deposit transaction together with a payout address to the TEE by calling `clear_contract`, which will give the operator the deposit transactions of the players. Once these are confirmed on the Zcash blockchain, the operator should call `finalize` to receive the settlement transaction that redeems their deposit. If any of the counterparties abort the contract, the operator should call `refund` to get their deposit back. See [the paper](https://eprint.iacr.org/2025/1965) for details.

The flow for the players is similar. Each player should first call `initialize` to create the Zcash wallets and obtain the deposit address. Having made the deposit, they should input the deposit transaction together with the input to the functionality to the TEE by calling `deposit_and_input`. The player should then fetch the counterparties' deposits via `get_deposits` and confirm they have all been broadcast to the Zcash blockchain by calling `confirm_deposits`. Once the settlement transaction has been broadcast and buried under sufficiently many blocks, the player should settle the contract by calling `settle`. If any of the counterparties abort the contract, the player should call `refund` to get their deposit back.
