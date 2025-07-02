
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
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
    static char reconstructed_line[MAX_LINE];
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

// 新增：重定向解析函数
void parse_redirection(char **args, char **input_file, char **output_file) {
    *input_file = NULL;
    *output_file = NULL;
    
    int i = 0;
    while (args[i] != NULL) {
        if (strcmp(args[i], "<") == 0) {
            if (args[i+1] == NULL) {
                fprintf(stderr, "语法错误: 缺少输入文件\n");
            } else {
                *input_file = args[i+1];
                args[i] = NULL; // 移除重定向符号
                args[i+1] = NULL; // 移除文件名
            }
        }
        else if (strcmp(args[i], ">") == 0) {
            if (args[i+1] == NULL) {
                fprintf(stderr, "语法错误: 缺少输出文件\n");
            } else {
                *output_file = args[i+1];
                args[i] = NULL;
                args[i+1] = NULL;
            }
        }
        i++;
    }
}

// 新增：压缩参数数组（移除NULL）
void compress_args(char **args) {
    int i = 0, j = 0;
    while (args[i]) {
        if (args[i] != NULL) {
            args[j++] = args[i];
        }
        i++;
    }
    args[j] = NULL;
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
// 新增管道执行函数
void execute_pipeline(char **args, int pipe_count, int background, int is_builtin_cmd, const char *raw_line) {
    // 分割命令
    // 创建命令数组（二维数组），注意：不能初始化，所以手动置NULL
    char *commands[pipe_count + 1][MAX_ARGS];
    // 初始化数组为NULL
    for (int i = 0; i < pipe_count + 1; i++) {
        for (int j = 0; j < MAX_ARGS; j++) {
            commands[i][j] = NULL;
        }
    }
    int cmd_index = 0, arg_index = 0;
    
    for (int i = 0; args[i]; i++) {
        if (strcmp(args[i], "|") == 0) {
            commands[cmd_index][arg_index] = NULL;
            cmd_index++;
            arg_index = 0;
        } else {
            commands[cmd_index][arg_index++] = args[i];
        }
    }
    commands[cmd_index][arg_index] = NULL;
    int cmd_total = cmd_index + 1;
    
    // 创建管道
    int pipes[pipe_count][2];
    for (int i = 0; i < pipe_count; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe failed");
            return;
        }
    }
    
    pid_t pids[cmd_total];
    for (int i = 0; i < cmd_total; i++) {
        pids[i] = fork();
        
        if (pids[i] < 0) {
            perror("fork failed");
            return;
        } else if (pids[i] == 0) {
            // 子进程 - 设置管道连接
            if (i > 0) {
                dup2(pipes[i-1][0], STDIN_FILENO); // 前一个命令的输出
            }
            if (i < cmd_total - 1) {
                dup2(pipes[i][1], STDOUT_FILENO); // 当前命令的输出
            }
            
            // 关闭所有管道描述符
            for (int j = 0; j < pipe_count; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            
            // 处理重定向
            char *input_file = NULL;
            char *output_file = NULL;
            parse_redirection(commands[i], &input_file, &output_file);
            compress_args(commands[i]);
            
            if (input_file) {
                int fd = open(input_file, O_RDONLY);
                if (fd < 0) {
                    perror("打开输入文件失败");
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            
            if (output_file) {
                int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    perror("创建输出文件失败");
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
            
            // 执行命令
            if (is_builtin(commands[i][0]) && 
                (strcmp(commands[i][0], "cd") == 0 || 
                 strcmp(commands[i][0], "alias") == 0 || 
                 strcmp(commands[i][0], "unalias") == 0)) {
                // 管道中的特殊内置命令需要特殊处理
                run_builtin(commands[i], raw_line);
                exit(0);
            } else {
                execvp(commands[i][0], commands[i]);
                perror(commands[i][0]);
                exit(EXIT_FAILURE);
            }
        }
    }
    
    // 父进程 - 关闭所有管道并等待
    for (int i = 0; i < pipe_count; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    
    if (!background) {
        for (int i = 0; i < cmd_total; i++) {
            waitpid(pids[i], NULL, 0);
        }
    } else {
        fprintf(stderr, "[Pipeline %d] running in background\n", getpid());
    }
}
int main() {
    char *line;
    char *args[MAX_ARGS];
    char command_buffer[MAX_COMMAND_LENGTH];
    char temp_line[MAX_COMMAND_LENGTH];

    signal(SIGINT, handle_sigint);

    if (!login_shell()) return 1;

    load_history_from_file();
    load_aliases_from_file();

    while (1) {
        show_prompt();
        command_buffer[0] = '\0';
        temp_line[0] = '\0';

        line = read_input_line();
        if (!line) continue;

        strncpy(temp_line, line, sizeof(temp_line) - 1);
        temp_line[sizeof(temp_line) - 1] = '\0';
        temp_line[strcspn(temp_line, "\n")] = '\0';
        strncpy(command_buffer, temp_line, sizeof(command_buffer) - 1);

        while (strlen(temp_line) > 0 && temp_line[strlen(temp_line) - 1] == '\\') {
            temp_line[strlen(temp_line) - 1] = '\0';
            size_t current_len = strlen(command_buffer);
            if (current_len > 0 && command_buffer[current_len - 1] == '\\') {
                command_buffer[current_len - 1] = '\0';
            }
            free(line);
            line = read_input_line();
            if (!line) break;
            strncpy(temp_line, line, sizeof(temp_line) - 1);
            temp_line[sizeof(temp_line) - 1] = '\0';
            temp_line[strcspn(temp_line, "\n")] = '\0';
            if (strlen(command_buffer) + strlen(temp_line) >= sizeof(command_buffer) - 1) {
                fprintf(stderr, "\u9519\u8bef\uff1a\u547d\u4ee4\u8fc7\u957f\uff0c\u8d85\u51fa\u7f13\u51b2\u533a\u9650\u5236\uff01\n");
                free(line);
                command_buffer[0] = '\0';
                break;
            }
            strcat(command_buffer, temp_line);
        }

        free(line);
        line = strdup(command_buffer);

        if (!line || !is_valid_command(line)) {
            free(line);
            continue;
        }

        char *line_copy = strdup(line);
        parse_and_expand_alias(line, args);
        if (args[0] == NULL) {
            free(line);
            free(line_copy);
            continue;
        }

        filter_and_add_history(line_copy);

        if (strcmp(args[0], "exit") == 0) {
            free(line);
            free(line_copy);
            break;
        }

        int background = 0;
        int i = 0;
        while (args[i]) i++;
        if (i > 0 && strcmp(args[i - 1], "&") == 0) {
            background = 1;
            args[i - 1] = NULL;
        }

        int is_builtin_cmd = is_builtin(args[0]);
        if (is_builtin_cmd) {
            // cd, alias, unalias, history 等应在主进程运行
            if (strcmp(args[0], "cd") == 0 ||
                strcmp(args[0], "alias") == 0 ||
                strcmp(args[0], "unalias") == 0 ||
                strcmp(args[0], "history") == 0 ||
                strcmp(args[0], "clearhistory") == 0) {
                run_builtin(args, line_copy);
                free(line);
                free(line_copy);
                continue;
            }
        }

        // ============= 修改开始 =============
        // 检查是否有管道
        int pipe_count = 0;
        for (int i = 0; args[i]; i++) {
            if (strcmp(args[i], "|") == 0) pipe_count++;
        }

        if (pipe_count > 0) {
            execute_pipeline(args, pipe_count, background, is_builtin_cmd, line_copy);
        } else {
            // 没有管道时执行单个命令
            pid_t pid = fork();
            if (pid < 0) {
                perror("fork failed");
            } else if(pid == 0) {
                // 子进程处理重定向
                char *input_file = NULL;
                char *output_file = NULL;
                parse_redirection(args, &input_file, &output_file);
                compress_args(args);
                
                if (input_file) {
                    int fd = open(input_file, O_RDONLY);
                    if (fd < 0) {
                        perror("打开输入文件失败");
                        exit(EXIT_FAILURE);
                    }
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }
                
                if (output_file) {
                    int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd < 0) {
                        perror("创建输出文件失败");
                        exit(EXIT_FAILURE);
                    }
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
                
                // 执行命令
                if (is_builtin_cmd) {
                    run_builtin(args, line_copy);
                } else {
                    execvp(args[0], args);
                }
                exit(1);
            } else {
                if (background) {
                    usleep(1000);
                    fprintf(stderr, "[PID %d] running in background\n", pid);
                    fflush(stderr);
                } else {
                    int status;
                    waitpid(pid, &status, 0);
                    if (!is_builtin_cmd && WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                        fprintf(stderr, "Unknown command: %s\n", args[0]);
                    }
                }
            }
        }
        // ============= 修改结束 =============

        free(line);
        free(line_copy);
    }

    save_history_to_file();
    return 0;
}


