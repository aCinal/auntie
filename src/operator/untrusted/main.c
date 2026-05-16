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
    fprintf(stderr, BOLD("%s") ": " UNDERLINE("operator-port") " " UNDERLINE("quotes-file") "\n", exename);
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

static inline int create_listening_socket(const char *sport)
{
    struct sockaddr_in sa = {};
    socklen_t addr_len;
    int sockfd;
    uint16_t port;
    char *endptr;

    port = (uint16_t) strtol(sport, &endptr, 10);
    if (*endptr) {
        fprintf(stderr, "Bad port: %s\n", sport);
        exit(EXIT_FAILURE);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    /* Assign a name to the socket */
    if (bind(sockfd, (const struct sockaddr *) &sa, sizeof(sa))) {
        perror("bind");
        (void) close(sockfd);
        exit(EXIT_FAILURE);
    }
    /* Start listening */
    if (listen(sockfd, 0)) {
        perror("listen");
        (void) close(sockfd);
        exit(EXIT_FAILURE);
    }

    /* Get back the port in case it was dynamically assigned */
    addr_len = sizeof(sa);
    if (getsockname(sockfd, (struct sockaddr *) &sa, &addr_len)) {
        perror("getsockname");
        (void) close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Server socket open and bound at port %d\n", htons(sa.sin_port));
    return sockfd;
}

static int accept_player_connection(int sockfd)
{
    int connfd;
    char client_ip[16];
    struct sockaddr_in client_addr;
    socklen_t addr_len;
    struct connection *connection;
    int ret;

    addr_len = sizeof(client_addr);
    connfd = accept(sockfd, (struct sockaddr *) &client_addr, &addr_len);
    if (connfd == -1) {
        perror("accept");
        return 0;
    }

    /* Get human-readable IPv4 address for logging purposes */
    (void) inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    client_ip[sizeof(client_ip) - 1] = '\0';
    printf("Connection accepted from %s:%d\n", \
        client_ip, ntohs(client_addr.sin_port));

    connection = malloc(sizeof(struct connection));
    if (!connection) {
        perror("malloc");
        close(connfd);
        return 0;
    }

    connection->fd = connfd;
    connection->peer = client_addr;

    /* Establish a secure channel */
    ret = ECALL(ecall_connect_to_player, connection);
    if (ret < 0) {
        fprintf(stderr, "Failed to establish a secure channel with error %d\n", ret);
        /* connection has been released in ocall_drop_connection */
        return 0;
    }

    return 1;
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

static void handle_clear_contract(int argc, char **argv)
{
    uint8_t *payout_address;
    uint8_t *collateral_transaction;
    uint8_t *merkle_paths;
    size_t payout_address_length;
    size_t collateral_transaction_length;
    size_t merkle_paths_length;
    FILE *deposit_transactions_file;
    int ret;

    if (argc < 5) {
        printf("%s: payout address, collateral transaction, deposit transactions, or the Merkle paths file missing\n", __func__);
        return;
    }

    payout_address = read_entire_file(&payout_address_length, argv[1]);
    if (!payout_address)
        return;

    collateral_transaction = read_entire_file(&collateral_transaction_length, argv[2]);
    if (!collateral_transaction) {
        free(payout_address);
        return;
    }

    merkle_paths = read_entire_file(&merkle_paths_length, argv[3]);
    if (!merkle_paths) {
        free(payout_address);
        free(collateral_transaction);
        return;
    }

    deposit_transactions_file = fopen(argv[4], "w");
    if (!deposit_transactions_file) {
        perror(argv[4]);
        free(payout_address);
        free(collateral_transaction);
        free(merkle_paths);
        return;
    }

    ret = ECALL(ecall_clear_contract,
        deposit_transactions_file,
        payout_address,
        payout_address_length,
        collateral_transaction,
        collateral_transaction_length,
        merkle_paths,
        merkle_paths_length
    );
    if (ret)
        printf("%s: ecall_clear_contract failed with error %d\n", __func__, ret);

    (void) fclose(deposit_transactions_file);
    free(payout_address);
    free(collateral_transaction);
    free(merkle_paths);
}

static void handle_finalize(int argc, char **argv)
{
    FILE *settlement_transaction_file;
    int ret;

    if (argc < 2) {
        printf("%s: settlement transaction file missing\n", __func__);
        return;
    }

    settlement_transaction_file = fopen(argv[1], "w");
    if (!settlement_transaction_file) {
        perror(argv[1]);
        return;
    }

    ret = ECALL(ecall_finalize, settlement_transaction_file);
    if (ret)
        printf("%s: ecall_finalize failed with error %d\n", __func__, ret);

    (void) fclose(settlement_transaction_file);
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
    int sockfd;
    int connections_accepted;

    if (argc < 3)
        usage(argv[0]);

    quotes_file = fopen(argv[2], "w");
    if (!quotes_file) {
        perror(argv[2]);
        exit(EXIT_FAILURE);
    }

    printf("Launching enclave '%s' in %s mode\n",
           OPERATOR_ENCLAVE_SO, SGX_DEBUG_FLAG ? "debug" : "release");
    if (create_enclave(OPERATOR_ENCLAVE_SO, SGX_DEBUG_FLAG))
        exit(EXIT_FAILURE);

    /* Start a TCP server */
    sockfd = create_listening_socket(argv[1]);

    printf("Waiting for %d player(s) to connect\n", AUNTIE_NUM_PLAYERS);
    connections_accepted = 0;
    /* Accept connections from all players */
    while (connections_accepted < AUNTIE_NUM_PLAYERS)
        connections_accepted += accept_player_connection(sockfd);

    /* All players connected, so we have all the quotes and can close the file */
    (void) fclose(quotes_file);
    quotes_file = NULL;

    /* Expose the interface to the TEE via a shell */
    register_shell_command(
        "refund",
        UNDERLINE("blocks") " " UNDERLINE("deposit-key-file") " / Back out of the contract and release the deposit",
        handle_refund
    );
    register_shell_command(
        "finalize",
        UNDERLINE("settlement-tx-file") " / Finalize the contract by authorizing the settlement transaction and releasing it to the operator",
        handle_finalize
    );
    register_shell_command(
        "clear_contract",
        UNDERLINE("payout-address") " " UNDERLINE("collateral-tx") " " UNDERLINE("merkle-paths") " " UNDERLINE("deposits-file") " / Clear the contract by evaluating the functionality and creating the settlement transaction",
        handle_clear_contract
    );
    register_shell_command(
        "initialize",
        UNDERLINE("deposit-address-file") " / Initialize the TEE and create the deposit wallet",
        handle_initialize
    );

    catch_sigint();
    run_shell();
}
