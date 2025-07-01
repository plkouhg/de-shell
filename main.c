#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include "builtin.h"
#include "input.h"

#define MAX_LINE 1024
#define MAX_ARGS 64

void handle_sigint(int sig) {
    printf("\n");
    show_prompt();
    fflush(stdout);
}

void parse_and_expand_alias(char *line, char **args) {
    static char reconstructed_line[MAX_LINE];  // 全局静态变量，防止内存失效
    memset(reconstructed_line, 0, sizeof(reconstructed_line));

    char original_line[MAX_LINE];
    strncpy(original_line, line, sizeof(original_line) - 1);
    original_line[sizeof(original_line) - 1] = '\0';

    char *first_token = strtok(original_line, " \t\n");

    if (!first_token) {
        args[0] = NULL;
        return;
    }

    const char *alias_cmd = resolve_alias(first_token);

    if (alias_cmd) {
        strncat(reconstructed_line, alias_cmd, sizeof(reconstructed_line) - 1);
        char *rest = line + (first_token - original_line) + strlen(first_token);
        strncat(reconstructed_line, rest, sizeof(reconstructed_line) - strlen(reconstructed_line) - 1);
    } else {
        strncpy(reconstructed_line, line, sizeof(reconstructed_line) - 1);
    }

    int i = 0;
    args[i] = strtok(reconstructed_line, " \t\n");
    while (args[i] != NULL && i < MAX_ARGS - 1) {
        args[++i] = strtok(NULL, " \t\n");
    }
}

int is_valid_command(char *line) {
    if (line == NULL || strlen(line) == 0) return 0;
    for (char *p = line; *p; p++) {
        if (*p != ' ' && *p != '\t') return 1;
    }
    return 0;
}

void show_prompt() {
    char cwd[256];
    char hostname[256];
    char *username = getenv("USER");

    getcwd(cwd, sizeof(cwd));
    gethostname(hostname, sizeof(hostname));

    if (geteuid() == 0) {
        printf("\033[1;31m%s\033[0m@\033[1;35m%s\033[0m:\033[1;34m%s\033[0m# ", 
               username, hostname, cwd);
    } else {
        printf("\033[1;32m%s\033[0m@\033[1;36m%s\033[0m:\033[1;34m%s\033[0m$ ", 
               username, hostname, cwd);
    }
    fflush(stdout);
}

int login_shell() {
    char input_user[50], input_pass[50];
    char *real_user = getenv("USER");

    printf("Username: ");
    fgets(input_user, sizeof(input_user), stdin);
    input_user[strcspn(input_user, "\n")] = 0;

    printf("Password: ");
    fgets(input_pass, sizeof(input_pass), stdin);
    input_pass[strcspn(input_pass, "\n")] = 0;

    if (strcmp(input_user, real_user) == 0) {
        return 1;
    } else {
        printf("Login failed.\n");
        return 0;
    }
}

int main() {
    char *line;
    char *args[MAX_ARGS];

    signal(SIGINT, handle_sigint);

    if (!login_shell()) return 1;

    //load_history_from_file();
    load_aliases_from_file();
    while (1) {
        show_prompt();
        line = read_input_line();

        if (!line || !is_valid_command(line)) {
            free(line);
            continue;
        }

        char *line_copy = strdup(line);  // 保存原始命令用于历史记录

        parse_and_expand_alias(line,args);
        if (args[0] == NULL) {
            free(line);
            free(line_copy);
            continue;
        }

        filter_and_add_history(line_copy);  // 使用校验后的命令添加到历史记录

        if (strcmp(args[0], "exit") == 0) {
            free(line);
            free(line_copy);
            break;
        }

        if (handle_builtin(args,line_copy)) {
            free(line);
            free(line_copy);
            continue;
        }

        fprintf(stderr, "Unknown command: %s\n", args[0]);
        free(line);
        free(line_copy);
    }

    save_history_to_file();
    return 0;
}
