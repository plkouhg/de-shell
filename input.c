#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <fnmatch.h>
#include "builtin.h"
#include "input.h" 

//#define MAX_INPUT 1024
#define MAX_ARGS 128
extern char *history[HISTORY_SIZE];
extern int history_count;

static int is_valid_command(const char *cmd) {
    if (!cmd || !*cmd) return 0;
    // 跳过前导空格
    while (isspace(*cmd)) cmd++;
    if (!*cmd) return 0;
    // 检查非法字符
    for (const char *p = cmd; *p; p++) {
        if (!isprint(*p) && !isspace(*p)) return 0;
    }
    // 特殊检查：cd 后必须跟空格
    if (strncmp(cmd, "cd", 2) == 0 && cmd[2] != ' ' && cmd[2] != '\0') {
        return 0;
    }
    return 1;
}

// 修改 filter_and_add_history
void filter_and_add_history(const char *cmd) {
    if (!is_valid_command(cmd)) return;
    // 检查整个历史记录是否已存在(目前不需要)
    //for (int i = 0; i < history_count; i++) {
    //    if (strcmp(history[i], cmd) == 0) return;
    //}
    add_history(cmd);
}

char *read_input_line() {
    static int history_index = -1;
    static char buffer[MAX_INPUT];
    int pos = 0;
    buffer[0] = '\0';

    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    int tab_count = 0;

    while (1) {
        char ch;
        if (read(STDIN_FILENO, &ch, 1) <= 0) continue;
        if (ch == '\n') {
            buffer[pos] = '\0';
            printf("\n");
            break;

        }

        // 上下方向键（历史）
        if (ch == 27) {
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;
            if (seq[0] == '[' && (seq[1] == 'A' || seq[1] == 'B')) {
                printf("\033[2K\r");
                if (seq[1] == 'A') {
                    if (history_count > 0) {
                        if (history_index == -1)
                            history_index = history_count - 1;
                        else if (history_index > 0)
                            history_index--;
                        strncpy(buffer, history[history_index], MAX_INPUT);
                        pos = strlen(buffer);
                    }
                } else if (seq[1] == 'B') {
                    if (history_index != -1) {
                        if (history_index < history_count - 1) {
                            history_index++;
                            strncpy(buffer, history[history_index], MAX_INPUT);
                            pos = strlen(buffer);
                        } else {
                            history_index = -1;
                            buffer[0] = '\0';
                            pos = 0;
                        }
                    }
                }
                show_prompt();
                printf("%s", buffer);
                fflush(stdout);
                continue;
            }
        }
        // Backspace

        if (ch == 127 || ch == '\b') {
            if (pos > 0) {
                pos--;
                buffer[pos] = '\0';
                printf("\b \b");
                fflush(stdout);
            }
            continue;

        }
        // TAB 补全逻辑

        if (ch == '\t') {
            tab_count++;
            buffer[pos] = '\0';
            char *last_space = strrchr(buffer, ' ');
            char *prefix = last_space ? last_space + 1 : buffer;
            int plen = strlen(prefix);
            int is_first_token = (last_space == NULL);
            char *matches[256];
            int match_count = 0;


            // === 1. 首词 → 补全命令 ===

            if (is_first_token && prefix[0] != '$') {
                const char *builtins[] = {"cd", "ls", "cat", "echo", "alias", "unalias", "grep", "type", "history", "clearhistory", NULL};
                for (int i = 0; builtins[i]; i++) {
                    if (strncmp(builtins[i], prefix, plen) == 0)
                        matches[match_count++] = (char *)builtins[i];
                }


                // 可执行文件 (PATH)
                /*
                char *path = getenv("PATH");
                if (path) {
                    char *copy = strdup(path);
                    char *dir = strtok(copy, ":");
                    while (dir) {
                        DIR *dp = opendir(dir);
                        if (dp) {
                            struct dirent *entry;
                            while ((entry = readdir(dp))) {
                                if (strncmp(entry->d_name, prefix, plen) == 0)
                                    matches[match_count++] = entry->d_name;
                            }

                            closedir(dp);
                        }
                        dir = strtok(NULL, ":");
                    }

                    free(copy);
                }*/

            }
            // === 2. 非首词补全 → 文件目录/名
          if (!is_first_token && prefix[0] != '$') {
              char dir_part[512] = ".", file_part[256] = "", path_buf[1024], full_path[1024];

              if (prefix[0] == '/' && !strchr(prefix + 1, '/')) {
                  // 仅输入了home的路径
                  strcpy(dir_part, "/");
                  strncpy(file_part, prefix + 1, sizeof(file_part));
                  file_part[sizeof(file_part) - 1] = '\0';
              } else {
                  const char *last_slash = strrchr(prefix, '/');
                  if (last_slash) {
                      int dir_len = last_slash - prefix;
                      if (dir_len >= sizeof(dir_part)) dir_len = sizeof(dir_part) - 1;
                      strncpy(dir_part, prefix, dir_len);
                      dir_part[dir_len] = '\0';
                      strncpy(file_part, last_slash + 1, sizeof(file_part));
                      file_part[sizeof(file_part) - 1] = '\0';
                  } else {
                      strncpy(file_part, prefix, sizeof(file_part));
                      file_part[sizeof(file_part) - 1] = '\0';
                      strcpy(dir_part, ".");
                  }
              }
              // 判断当前是否是 cd 命令（仅补目录）
              int is_cd_command = 0;
              {
                  char buf_copy[512];
                  strncpy(buf_copy, buffer, sizeof(buf_copy));
                  buf_copy[sizeof(buf_copy) - 1] = '\0';
                  char *token = strtok(buf_copy, " \t");
                  if (token && strcmp(token, "cd") == 0)
                      is_cd_command = 1;
              }

              DIR *dp = opendir(dir_part[0] ? dir_part : ".");
              if (dp) {
                  struct dirent *entry;
                  while ((entry = readdir(dp))) {
                      if (strncmp(entry->d_name, file_part, strlen(file_part)) == 0) {
                          snprintf(path_buf, sizeof(path_buf), "%s/%s", dir_part, entry->d_name);
                          struct stat st;
                          if (stat(path_buf, &st) == 0) {
                              if (is_cd_command) {
                                  if (S_ISDIR(st.st_mode)) {
                                      snprintf(full_path, sizeof(full_path), "%s/%s", dir_part, entry->d_name);
                                      matches[match_count++] = strdup(full_path);
                                  }
                              } else {
                                  snprintf(full_path, sizeof(full_path), "%s/%s", dir_part, entry->d_name);
                                  matches[match_count++] = strdup(full_path);
                              }
                          }
                      }
                  }
                  closedir(dp);
              }
          }
            // === 3. 环境变量补全
            if (prefix[0] == '$') {
                extern char **environ;
                for (int i = 0; environ[i]; i++) {
                    char *eq = strchr(environ[i], '=');
                    if (eq && strncmp(environ[i], prefix + 1, plen - 1) == 0) {
                        int len = eq - environ[i];
                        static char var[128];
                        snprintf(var, sizeof(var), "$%.*s", len, environ[i]);
                        matches[match_count++] = var;
                    }

                }
            }
            if (match_count == 1) {
                const char *completion = matches[0];
                int cplen = strlen(completion);
                pos = (last_space ? (last_space - buffer + 1) : 0);
                strcpy(&buffer[pos], completion);
                pos += cplen;
                buffer[pos] = '\0';
                printf("\033[2K\r");
                show_prompt();
                printf("%s", buffer);
                fflush(stdout);
                tab_count = 0;

            } else if (match_count > 1 && tab_count >= 2) {
                printf("\n");
                for (int i = 0; i < match_count; i++) {
                    printf("%s  ", matches[i]);
                }
                printf("\n");
                show_prompt();
                printf("%s", buffer);
                fflush(stdout);
                tab_count = 0;
            }
            continue;
        }

        // 普通可见字符
        if (pos < MAX_INPUT - 1 && isprint(ch)) {
            buffer[pos++] = ch;
            buffer[pos] = '\0';
            putchar(ch);
            fflush(stdout);
            tab_count = 0;
        }
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    history_index = -1;
    return strdup(buffer);

}


#define MAX_ARGS 128

char **expand_args(char **args) {
    static char *new_args[MAX_ARGS];
    int new_argc = 0;

    for (int i = 0; args[i] != NULL; i++) {
        if (strpbrk(args[i], "*?[") != NULL) {
            // 含通配符，尝试匹配
            DIR *dir = opendir(".");
            if (!dir) continue;
            struct dirent *entry;
            int match_found = 0;

            while ((entry = readdir(dir))) {
                if (fnmatch(args[i], entry->d_name, 0) == 0) {
                    new_args[new_argc++] = strdup(entry->d_name);
                    match_found = 1;
                    if (new_argc >= MAX_ARGS - 1) break;
                }
            }

            closedir(dir);

            if (!match_found) {
                // 无匹配时保留原始参数，防止如 cat t*.txt 卡死
                new_args[new_argc++] = strdup(args[i]);
            }

        } else {
            new_args[new_argc++] = strdup(args[i]);
        }
    }

    // 如果是仅输入了 "ls" 或 "ls -l"，补一个 "."
    if (new_argc > 0 && strcmp(new_args[0], "ls") == 0) {
        int has_file_arg = 0;
        for (int i = 1; i < new_argc; ++i) {
            if (new_args[i][0] != '-') {
                has_file_arg = 1;
                break;
            }
        }
        if (!has_file_arg) {
            new_args[new_argc++] = strdup(".");
        }
    }

    new_args[new_argc] = NULL;
    return new_args;
}







