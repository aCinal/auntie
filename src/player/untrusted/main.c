#include "ecall.h"
#include "connection.h"
#include "pretty.h"
#include "shell.h"
#include "utils.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>

extern FILE *quotes_file;

static inline void usage(const char *exename)
{
    fprintf(stderr, BOLD("%s") ": " UNDERLINE("player-id") " " UNDERLINE("operator-ip:port") " " UNDERLINE("quotes-file") "\n", exename);
    exit(EXIT_FAILURE);
}

static void sigint_handler(int signo)
{
    (void) signo;
    /* Define an empty handler so that blocking calls such as socket reads get interrupted
     * allowing the host to stop waiting for counterparties' messages and request a refund */
}

static inline void catch_sigint(void)
{
    struct sigaction sa = { .sa_handler = sigint_handler };
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL)) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

static void connect_to_operator(char *address, uint32_t player_id)
{
    const char *ip;
    uint16_t port;
    char *endptr;
    int sockfd;
    struct sockaddr_in sa = {};
    struct connection *connection;
    int ret;

    ip = strtok(address, ":");
    address = strtok(NULL, ":");

    if (!address) {
        fprintf(stderr, "Invalid operator address\n");
        exit(EXIT_FAILURE);
    }

    port = (uint16_t) strtol(address, &endptr, 10);
    if (*endptr) {
        fprintf(stderr, "Bad port: %s\n", address);
        exit(EXIT_FAILURE);
    }

    /* Open a socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if (1 != inet_pton(AF_INET, ip, &sa.sin_addr.s_addr)) {
        fprintf(stderr, "Bad IP address: %s\n", ip);
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    /* Connect to the server */
    if (connect(sockfd, (const struct sockaddr *) &sa, sizeof(sa))) {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    connection = malloc(sizeof(struct connection));
    if (!connection) {
        perror("malloc");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    connection->fd = sockfd;
    connection->peer = sa;

    ret = ECALL(ecall_connect_to_operator, connection, player_id);
    if (ret) {
        printf("Failed to establish secure channel with the operator due to error %d\n", ret);
        exit(EXIT_FAILURE);
    }
}

static void handle_initialize(int argc, char **argv)
{
    FILE *deposit_address_file;
    int ret;

    if (argc < 2) {
        printf("%s: deposit file missing\n", __func__);
        return;
    }

    deposit_address_file = fopen(argv[1], "w");
    if (!deposit_address_file) {
        perror(argv[1]);
        return;
    }

    ret = ECALL(ecall_initialize, deposit_address_file);
    if (ret)
        printf("%s: ecall_initialize failed with error %d\n", __func__, ret);

    (void) fclose(deposit_address_file);
}

static void handle_deposit_and_input(int argc, char **argv)
{
    uint8_t *deposit_transaction;
    uint8_t *functionality_input;
    size_t deposit_transaction_length;
    size_t functionality_input_length;
    int ret;

    if (argc < 3) {
        printf("%s: deposit transaction or functionality input missing\n", __func__);
        return;
    }

    deposit_transaction = read_entire_file(&deposit_transaction_length, argv[1]);
    if (!deposit_transaction)
        return;

    functionality_input = read_entire_file(&functionality_input_length, argv[2]);
    if (!functionality_input) {
        free(deposit_transaction);
        return;
    }

    ret = ECALL(ecall_deposit_and_input,
        deposit_transaction,
        deposit_transaction_length,
        functionality_input,
        functionality_input_length
    );
    if (ret)
        printf("%s: ecall_deposit_and_input failed with error %d\n", __func__, ret);

    free(deposit_transaction);
    free(functionality_input);
}

static void handle_get_deposits(int argc, char **argv)
{
    FILE *deposit_transactions_file;
    int ret;

    if (argc < 2) {
        printf("%s: deposit transactions file missing\n", __func__);
        return;
    }

    deposit_transactions_file = fopen(argv[1], "w");
    if (!deposit_transactions_file) {
        perror(argv[1]);
        return;
    }

    ret = ECALL(ecall_get_deposits, deposit_transactions_file);
    if (ret)
        printf("%s: ecall_get_deposits failed with error %d\n", __func__, ret);

    (void) fclose(deposit_transactions_file);
}

static void handle_confirm_deposits(int argc, char **argv)
{
    int ret;

    (void) argc;
    (void) argv;

    ret = ECALL(ecall_confirm_deposits);
    if (ret)
        printf("%s: ecall_confirm_deposits failed with error %d\n", __func__, ret);
}

static void handle_settle(int argc, char **argv)
{
    uint8_t *blocks;
    size_t blocks_length;
    FILE *settlement_file;
    int ret;

    if (argc < 3) {
        printf("%s: blocks or settlement file missing\n", __func__);
        return;
    }

    blocks = read_entire_file(&blocks_length, argv[1]);
    if (!blocks)
        return;

    settlement_file = fopen(argv[2], "w");
    if (!settlement_file) {
        perror(argv[1]);
        free(blocks);
        return;
    }

    ret = ECALL(ecall_settle, settlement_file, blocks, blocks_length);
    if (ret)
        printf("%s: ecall_settle failed with error %d\n", __func__, ret);

    (void) fclose(settlement_file);
    free(blocks);
}

static void handle_refund(int argc, char **argv)
{
    uint8_t *blocks;
    size_t blocks_length;
    FILE *deposit_key_file;
    int ret;

    if (argc < 3) {
        printf("%s: blocks or deposit key file missing\n", __func__);
        return;
    }

    blocks = read_entire_file(&blocks_length, argv[1]);
    if (!blocks)
        return;

    deposit_key_file = fopen(argv[2], "w");
    if (!deposit_key_file) {
        perror(argv[1]);
        free(blocks);
        return;
    }

    ret = ECALL(ecall_refund, deposit_key_file, blocks, blocks_length);
    if (ret)
        printf("%s: ecall_refund failed with error %d\n", __func__, ret);

    (void) fclose(deposit_key_file);
    free(blocks);
}

int main(int argc, char **argv)
{
    uint32_t player_id;
    char *endptr;

    if (argc < 4)
        usage(argv[0]);

    player_id = (uint32_t) strtol(argv[1], &endptr, 10);
    if (*endptr || !player_id || player_id > AUNTIE_NUM_PLAYERS) {
        fprintf(stderr, "Bad player ID: %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    quotes_file = fopen(argv[3], "w");
    if (!quotes_file) {
        perror(argv[3]);
        exit(EXIT_FAILURE);
    }

    printf("Launching enclave '%s' in %s mode\n",
           PLAYER_ENCLAVE_SO, SGX_DEBUG_FLAG ? "debug" : "release");
    if (create_enclave(PLAYER_ENCLAVE_SO, SGX_DEBUG_FLAG))
        exit(EXIT_FAILURE);

    printf("Connecting to the operator\n");
    connect_to_operator(argv[2], player_id);

    /* Connected to the operator, so we should have all the quotes and can close the file */
    (void) fclose(quotes_file);
    quotes_file = NULL;

    /* Expose the interface to the TEE via a shell */
    register_shell_command(
        "refund",
        UNDERLINE("blocks") " " UNDERLINE("deposit-key-file") " / Back out of the contract and release the deposit",
        handle_refund
    );
    register_shell_command(
        "settle",
        UNDERLINE("blocks") " " UNDERLINE("settlement-file") " / Settle the contract by releasing the payout key and the functionality output",
        handle_settle
    );
    register_shell_command(
        "confirm_deposits",
        "Confirm all deposits have been made",
        handle_confirm_deposits
    );
    register_shell_command(
        "get_deposits",
        UNDERLINE("deposits-file") " / Get deposits of other parties",
        handle_get_deposits
    );
    register_shell_command(
        "deposit_and_input",
        UNDERLINE("deposit-tx") " " UNDERLINE("functionality-input") " / Input the deposit transaction and the functionality input to the TEE",
        handle_deposit_and_input
    );
    register_shell_command(
        "initialize",
        UNDERLINE("deposit-address-file") " / Initialize the TEE and create the deposit and payout wallets",
        handle_initialize
    );

    catch_sigint();
    run_shell();
}
