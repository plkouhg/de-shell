#include <stdio.h>
#include <ctype.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include "builtin.h"
#include "input.h" 

#define MAX_INPUT 1024

extern char *history[HISTORY_SIZE];
extern int history_count;

// 在 input.c 中修改 is_valid_command 函数
static int is_valid_command(const char *cmd) {
    if (!cmd || !*cmd) return 0;
    
    // 跳过前导空格
    while (isspace(*cmd)) cmd++;
    if (!*cmd) return 0;
    
    // 检查非法字符
    for (const char *p = cmd; *p; p++) {
        if (!isprint(*p) && !isspace(*p)) return 0;
    }
    
    // 特殊检查：cd 后必须跟空格（暂时不需要）
    //if (strncmp(cmd, "cd", 2) == 0 && cmd[2] != ' ' && cmd[2] != '\0') {
    //    return 0;
    //}
    
    return 1;
}

// 修改 filter_and_add_history
void filter_and_add_history(const char *cmd) {
    if (!is_valid_command(cmd)) return;
    
    // 检查整个历史记录是否已存在
    for (int i = 0; i < history_count; i++) {
        if (strcmp(history[i], cmd) == 0) return;
    }
    
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

    while (1) {
        char ch;
        if (read(STDIN_FILENO, &ch, 1) <= 0) continue;

        if (ch == '\n') {
            buffer[pos] = '\0';
            printf("\n");
            break;
        }

        if (ch == 27) {
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;

            if (seq[0] == '[' && (seq[1] == 'A' || seq[1] == 'B')) {
                printf("\033[2K\r");
                
                if (seq[1] == 'A') {
                    if (history_count > 0) {
                        if (history_index == -1) {
                            history_index = history_count - 1;
                        } else if (history_index > 0) {
                            history_index--;
                        }
                        strncpy(buffer, history[history_index], MAX_INPUT);
                        pos = strlen(buffer);
                    }
                }
                else if (seq[1] == 'B') {
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

        if (ch == 127) {
            if (pos > 0) {
                pos--;
                buffer[pos] = '\0';
                printf("\b \b");
            }
            continue;
        }

        if (pos < MAX_INPUT - 1 && ch >= 32 && ch <= 126) {
            buffer[pos++] = ch;
            buffer[pos] = '\0';
            putchar(ch);
            fflush(stdout);
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    history_index = -1;
    return strdup(buffer);
}
