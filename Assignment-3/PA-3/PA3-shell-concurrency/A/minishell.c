#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_LINE 1024
#define MAX_ARGS 64

static int parse_command(char *line, char *args[])
{
    int argc = 0;
    char *token = strtok(line, " \t\r\n");

    while (token != NULL && argc < MAX_ARGS - 1) {
        args[argc++] = token;
        token = strtok(NULL, " \t\r\n");
    }

    args[argc] = NULL;
    return argc;
}

static void execute_external(char *args[])
{
    (void)args;

    /* TODO A3-A5:
     * 1. fork() a child.
     * 2. In the child, call execvp(args[0], args).
     * 3. If execvp() fails, print an error and terminate ONLY the child.
     * 4. In the parent, wait for that specific child using waitpid().
     * 5. Handle fork()/waitpid() errors.
     */

    fprintf(stderr, "TODO: external command execution is not implemented\n");
}

int main(void)
{
    char line[MAX_LINE];
    char *args[MAX_ARGS];

    while (1) {
        printf("msh> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            /* EOF (Ctrl-D or end of redirected input) exits the shell. */
            putchar('\n');
            break;
        }

        int argc = parse_command(line, args);
        if (argc == 0) {
            continue;
        }

        if (strcmp(args[0], "exit") == 0) {
            /* TODO A1: terminate the shell process cleanly. */
        }

        if (strcmp(args[0], "cd") == 0) {
            /* TODO A2:
             * - cd must be handled by the shell process itself.
             * - Require exactly one directory argument.
             * - Use chdir().
             * - Use perror("cd") if chdir() fails.
             * - Do not fall through to execute_external().
             */
        }

        execute_external(args);
    }

    return 0;
}
