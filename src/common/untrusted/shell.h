
#ifndef __SRC_COMMON_UNTRUSTED_SHELL_H
#define __SRC_COMMON_UNTRUSTED_SHELL_H

int register_shell_command(const char *name, const char *description, void (*callback)(int, char **));
void run_shell(void);

#endif /* __SRC_COMMON_UNTRUSTED_SHELL_H */
