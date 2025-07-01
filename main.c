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
#define MAX_COMMAND_LENGTH 2048

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
    char command_buffer[MAX_COMMAND_LENGTH];
    char temp_line[MAX_COMMAND_LENGTH];
    
    signal(SIGINT, handle_sigint);

    if (!login_shell()) return 1;

    //load_history_from_file();
    load_aliases_from_file();
    while (1) {
        show_prompt();
        command_buffer[0] = '\0'; // 清空命令缓冲区
        temp_line[0]='\0';
        line = read_input_line();
        if (!line) continue;

        // 处理第一行
        strncpy(temp_line, line, sizeof(temp_line) - 1);
        temp_line[sizeof(temp_line) - 1] = '\0';
        temp_line[strcspn(temp_line, "\n")] = '\0';
        
        // 初始化command_buffer
        strncpy(command_buffer, temp_line, sizeof(command_buffer) - 1);
        command_buffer[sizeof(command_buffer) - 1] = '\0';

        // 检查是否需要续行
        while (strlen(temp_line) > 0 && temp_line[strlen(temp_line) - 1] == '\\') {
            // 移除续行符
            temp_line[strlen(temp_line) - 1] = '\0';
            
            // 更新command_buffer（只移除最后的反斜杠）
            size_t current_len = strlen(command_buffer);
            if (current_len > 0 && command_buffer[current_len - 1] == '\\') {
                command_buffer[current_len - 1] = '\0';
            }

            // 读取下一行
            free(line); // 释放前一行内存
            line = read_input_line();
            if (!line) break;
            
            // 处理新行
            strncpy(temp_line, line, sizeof(temp_line) - 1);
            temp_line[sizeof(temp_line) - 1] = '\0';
            temp_line[strcspn(temp_line, "\n")] = '\0';

            // 检查缓冲区空间
            if (strlen(command_buffer) + strlen(temp_line) >= sizeof(command_buffer) - 1) {
                fprintf(stderr, "错误：命令过长，超出缓冲区限制！\n");
                free(line);
                command_buffer[0] = '\0'; // 清空无效命令
                break;
            }

            // 安全拼接
            strcat(command_buffer, temp_line);
        }


        free(line); // 释放旧内存
        line = strdup(command_buffer); // 创建新拷贝
        //printf("完整命令是: %s\n", line);
        //以下逻辑保持不变
        
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
