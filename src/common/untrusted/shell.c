#include "shell.h"
#include "pretty.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

struct command {
    char *name;
    char *description;
    void (*callback)(int, char **);
    struct command *next;
};

static size_t longest_name = 0;
static struct command *commands;

static void help_callback(int argc, char **argv)
{
    struct command *iter;
    size_t name_length;
    char padding[longest_name + 1];

    (void) argc;
    (void) argv;

    /* Prepare a padding buffer */
    (void) memset(padding, ' ', longest_name);
    padding[longest_name] = '\0';

    /* Iterate over all all commands and print their descriptions */
    iter = commands;
    while (iter) {

        name_length = strlen(iter->name);
        /* Cut the padding at an appropriate point */
        padding[longest_name - name_length] = '\0';
        printf("    %s" BOLD("%s") "    %s\n", padding, iter->name, iter->description);
        /* Restore the padding string */
        padding[longest_name - name_length] = ' ';
        iter = iter->next;
    }
}

static void quit_callback(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    printf("Bye!\n");
    exit(EXIT_SUCCESS);
}

static inline struct command *find_command(const char *name)
{
    struct command *iter;

    iter = commands;
    while (iter && strcmp(iter->name, name))
        iter = iter->next;
    return iter;
}

int register_shell_command(const char *name, const char *description, void (*callback)(int, char **))
{
    struct command *command;
    size_t name_length;

    if (!name || !callback)
        return -EINVAL;

    /* See if a command by this name has been registered before */
    if (find_command(name))
        return -EEXIST;

    /* Create the command structure */
    command = malloc(sizeof(struct command));
    if (!command)
        return -ENOMEM;

    name_length = strlen(name);
    command->name = malloc(name_length + 1);
    if (!command->name) {
        free(command);
        return -ENOMEM;
    }
    (void) strcpy(command->name, name);
    command->description = malloc(strlen(description) + 1);
    if (!command->description) {
        free(command->name);
        free(command);
        return -ENOMEM;
    }
    (void) strcpy(command->description, description);
    command->callback = callback;

    /* Register in the list */
    command->next = commands;
    commands = command;

    /* Keep track of the longest name for pretty printing alignment */
    if (name_length > longest_name)
        longest_name = name_length;

    return 0;
}

static char **tokenize(char *buffer, int *argc)
{
    char **argv;
    void *ret;
    char *token;

    *argc = 0;
    argv = NULL;

    token = strtok(buffer, " ");
    while (token) {
        *argc += 1;
        ret = realloc(argv, *argc * sizeof(char *));
        if (!ret) {
            free(argv);
            return NULL;
        }
        argv = ret;
        argv[*argc - 1] = token;
        token = strtok(NULL, " ");
    }

    return argv;
}

void run_shell(void)
{
    char buffer[1024];
    struct command *command;
    char **argv;
    int argc;

    /* Register basic commands, ignore errors in case they have been registered already by the caller */
    (void) register_shell_command("help", "Print help", help_callback);
    (void) register_shell_command("?", "Print help", help_callback);
    (void) register_shell_command("quit", "Exit the application", quit_callback);

    printf("Welcome to the Auntie shell! Type '?' or 'help' for more information, or 'quit' to exit.\n");

    for (; /* ever */ ;) {

        /* Read in terminal input */
        if (!fgets(buffer, sizeof(buffer), stdin)) {
            if (feof(stdin))
                exit(EXIT_SUCCESS);
            /* Ignore interruptions here, have the user tell us explicitly they want to quit */
            if (errno == EINTR)
                continue;
            perror("fgets");
            exit(EXIT_FAILURE);
        }

        /* Tokenize the buffer */
        argv = tokenize(buffer, &argc);
        if (!argv) {
            fprintf(stderr, "Tokenization failed\n");
            exit(EXIT_FAILURE);
        }
        /* Strip newline off the last token */
        argv[argc - 1][strlen(argv[argc - 1]) - 1] = '\0';

        /* Ignore empty input */
        if (!strlen(argv[0]))
            continue;

        /* Fetch the command and execute it */
        command = find_command(argv[0]);
        if (command)
            command->callback(argc, argv);
        else
            printf("Unknown command: %s\n", argv[0]);

        free(argv);
    }
}
