#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include "builtin.h"
#include <regex.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/stat.h>
#define MAX_LINE 1024
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_RESET   "\x1b[0m"
char *history[HISTORY_SIZE];
int history_count = 0;
Alias aliases[MAX_ALIASES];
int alias_count = 0;

void my_ls(char **args) {
    int long_format = 0;
    for (int i = 1; args[i]; i++) {
        if (strcmp(args[i], "-l") == 0) {
            long_format = 1;
        }
    }

    DIR *dir = opendir(".");
    if (!dir) {
        perror("opendir");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        if (!long_format) {
            printf("%s  ", entry->d_name);
        } else {
            struct stat st;
            if (stat(entry->d_name, &st) == 0) {
                printf("%c%c%c%c%c%c%c%c%c%c %3ld %-8s %-8s %8ld %s\n",
                    S_ISDIR(st.st_mode) ? 'd' : '-',
                    st.st_mode & S_IRUSR ? 'r' : '-',
                    st.st_mode & S_IWUSR ? 'w' : '-',
                    st.st_mode & S_IXUSR ? 'x' : '-',
                    st.st_mode & S_IRGRP ? 'r' : '-',
                    st.st_mode & S_IWGRP ? 'w' : '-',
                    st.st_mode & S_IXGRP ? 'x' : '-',
                    st.st_mode & S_IROTH ? 'r' : '-',
                    st.st_mode & S_IWOTH ? 'w' : '-',
                    st.st_mode & S_IXOTH ? 'x' : '-',
                    (long)st.st_nlink,
                    getpwuid(st.st_uid)->pw_name,
                    getgrgid(st.st_gid)->gr_name,
                    (long)st.st_size,
                    entry->d_name
                );
            }
        }
    }

    if (!long_format) printf("\n");

    closedir(dir);
}

void my_cd(char **args) {
    if (!args[1]) {
        fprintf(stderr, "cd: missing directory\n");
        return;
    }
    if (chdir(args[1]) != 0) {
        perror("cd");
    }
}

void my_cat(char **args) {
    int show_line_numbers = 0;
    int start_index = 1;
    if (args[1] && strcmp(args[1], "-n") == 0) {
        show_line_numbers = 1;
        start_index = 2;
    }

    if (!args[start_index]) {
        fprintf(stderr, "cat: missing filename\n");
        return;
    }

    for (int i = start_index; args[i]; i++) {
        FILE *fp = fopen(args[i], "r");
        if (!fp) {
            perror(args[i]);
            continue;
        }

        char line[1024];
        int line_num = 1;

        while (fgets(line, sizeof(line), fp)) {
            if (show_line_numbers) {
                printf("%6d  %s", line_num++, line);
            } else {
                fputs(line, stdout);
            }
        }

        fclose(fp);
    }
}

void my_echo(char **args) {
    for (int i = 1; args[i]; i++) {
        if (args[i][0] == '$') {
            char *env = getenv(args[i] + 1);
            if (env) printf("%s ", env);
        } else {
            // 处理转义字符
            char *str = args[i];
            while (*str) {
                if (*str == '\\') {
                    str++; // 跳过反斜杠
                    switch (*str) {
                        case 'n':  putchar('\n'); break;
                        case 't':  putchar('\t'); break;
                        case '\\': putchar('\\'); break;
                        case 'b':  putchar('\b'); break;
                        default:   //putchar('\\'); // 如果不是已知转义字符，不输出反斜杠
                                  putchar(*str);  // 和后面的字符
                                  break;
                    }
                    if (*str) str++; // 如果还有字符，继续处理
                } else {
                    putchar(*str++);
                }
            }
            printf(" ");
        }
    }
    printf("\n");
}

void my_grep(char **args) {
    // 初始化选项变量
    int ignore_case = 0;
    int invert_match = 0;
    int line_number = 0;
    int count_only = 0;
    int recursive = 0;
    int files_with_matches = 0;
    int only_matching = 0;
    int extended_regex = 0;
    int after_context = 0;
    int before_context = 0;
    int context_lines = 0;
    
    char *pattern = NULL;
    int pattern_found = 0;
    int file_args_start = 0;
    
    // 解析参数
    for (int i = 1; args[i] != NULL; i++) {
        if (strcmp(args[i], "-i") == 0) {
            ignore_case = 1;
        } else if (strcmp(args[i], "-v") == 0) {
            invert_match = 1;
        } else if (strcmp(args[i], "-n") == 0) {
            line_number = 1;
        } else if (strcmp(args[i], "-c") == 0) {
            count_only = 1;
        } else if (strcmp(args[i], "-r") == 0) {
            recursive = 1;
        } else if (strcmp(args[i], "-l") == 0) {
            files_with_matches = 1;
        } else if (strcmp(args[i], "-o") == 0) {
            only_matching = 1;
        } else if (strcmp(args[i], "-E") == 0) {
            extended_regex = 1;
        } else if (strcmp(args[i], "-A") == 0 && args[i+1] != NULL) {
            after_context = 1;
            context_lines = atoi(args[i+1]);
            i++;
        } else if (strcmp(args[i], "-B") == 0 && args[i+1] != NULL) {
            before_context = 1;
            context_lines = atoi(args[i+1]);
            i++;
        } else if (args[i][0] != '-' && !pattern_found) {
            pattern = args[i];
            pattern_found = 1;
            file_args_start = i + 1;
        }
    }
    
    // 检查参数有效性
    if (!pattern || !args[file_args_start]) {
        fprintf(stderr, "Usage: grep [-i] [-v] [-n] [-c] [-r] [-l] [-o] [-E] [-A num] [-B num] pattern file...\n");
        return;
    }
    
    // 处理正则表达式
    regex_t regex;
    int reg_flags = REG_EXTENDED | (ignore_case ? REG_ICASE : 0);
    if (regcomp(&regex, pattern, reg_flags) != 0) {
        fprintf(stderr, "Invalid regular expression\n");
        return;
    }
    
    // 处理文件参数
    for (int i = file_args_start; args[i] != NULL; i++) {
        process_file_or_dir(args[i], pattern, &regex, 
                          ignore_case, invert_match, line_number,
                          count_only, recursive, files_with_matches,
                          only_matching, extended_regex,
                          after_context, before_context, context_lines);
    }
    
    regfree(&regex);
}

// 辅助函数：处理文件或目录
void process_file_or_dir(const char *path, const char *pattern, regex_t *regex,
                        int ignore_case, int invert_match, int line_number,
                        int count_only, int recursive, int files_with_matches,
                        int only_matching, int extended_regex,
                        int after_context, int before_context, int context_lines) {
    struct stat statbuf;
    if (stat(path, &statbuf) != 0) {
        perror(path);
        return;
    }
    
    if (S_ISDIR(statbuf.st_mode)) {
        if (recursive) {
            process_directory(path, pattern, regex, 
                            ignore_case, invert_match, line_number,
                            count_only, files_with_matches,
                            only_matching, extended_regex,
                            after_context, before_context, context_lines);
        } else {
            fprintf(stderr, "grep: %s: Is a directory\n", path);
        }
    } else {
        process_file(path, pattern, regex, 
                    ignore_case, invert_match, line_number,
                    count_only, files_with_matches,
                    only_matching, extended_regex,
                    after_context, before_context, context_lines);
    }
}

// 辅助函数：处理目录
void process_directory(const char *dirpath, const char *pattern, regex_t *regex,
                      int ignore_case, int invert_match, int line_number,
                      int count_only, int files_with_matches,
                      int only_matching, int extended_regex,
                      int after_context, int before_context, int context_lines) {
    DIR *dir = opendir(dirpath);
    if (!dir) {
        perror(dirpath);
        return;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char fullpath[PATH_MAX];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, entry->d_name);
        
        process_file_or_dir(fullpath, pattern, regex, 
                          ignore_case, invert_match, line_number,
                          count_only, 1, files_with_matches,
                          only_matching, extended_regex,
                          after_context, before_context, context_lines);
    }
    
    closedir(dir);
}

// 辅助函数：处理单个文件
void process_file(const char *filename, const char *pattern, regex_t *regex,
                 int ignore_case, int invert_match, int line_number,
                 int count_only, int files_with_matches,
                 int only_matching, int extended_regex,
                 int after_context, int before_context, int context_lines) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror(filename);
        return;
    }
    
    char line[4096];
    int line_num = 0;
    int match_count = 0;
    int *matches = NULL;
    int lines_allocated = 0;
    int lines_printed = 0;
    
    // 如果需要上下文，我们需要存储匹配的行号
    if (after_context || before_context) {
        lines_allocated = 100;
        matches = malloc(lines_allocated * sizeof(int));
    }
    
    // 第一次遍历：查找匹配行
    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        int match = (regexec(regex, line, 0, NULL, 0) == 0);
        if (invert_match) match = !match;
        
        if (match) {
            if (count_only || files_with_matches) {
                match_count++;
            } else if (after_context || before_context) {
                // 存储匹配行号
                if (line_num >= lines_allocated) {
                    lines_allocated *= 2;
                    matches = realloc(matches, lines_allocated * sizeof(int));
                }
                matches[match_count++] = line_num;
            }
        }
    }
    
    // 处理 -l 和 -c 选项
    if (files_with_matches) {
        if (match_count > 0) {
            printf("%s\n", filename);
        }
        fclose(fp);
        if (matches) free(matches);
        return;
    }
    
    if (count_only) {
        printf("%s:%d\n", filename, match_count);
        fclose(fp);
        if (matches) free(matches);
        return;
    }
    
    // 第二次遍历：输出结果
    rewind(fp);
    line_num = 0;
    int last_printed_line = -1;
    
    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        int match = (regexec(regex, line, 0, NULL, 0) == 0);
        if (invert_match) match = !match;
        
        // 处理上下文
        int should_print = 0;
        
        if (match) {
            should_print = 1;
            if (before_context) {
                // 打印前N行
                int start = (line_num - context_lines > 1) ? line_num - context_lines : 1;
                for (int i = start; i < line_num; i++) {
                    if (i > last_printed_line) {
                        print_line(filename, i, line, line_number, 0, pattern);
                        last_printed_line = i;
                    }
                }
            }
        }
        
        if (match || 
            (after_context && lines_printed < match_count && line_num <= matches[lines_printed] + context_lines)) {
            should_print = 1;
        }
        
        if (should_print && line_num > last_printed_line) {
            print_line(filename, line_num, line, line_number, match && only_matching, pattern);
            last_printed_line = line_num;
            if (match) lines_printed++;
        }
    }
    
    fclose(fp);
    if (matches) free(matches);
}

// 辅助函数：打印一行
void print_line(const char *filename, int line_num, const char *line, 
               int show_line_number, int only_matching, char * pattern) {
    if (only_matching) {
        // 这里简化处理，实际需要提取匹配的部分
        printf("%s\n", line);  // 实际实现需要更复杂的处理
    } else {
        char *match_start = strstr(line, pattern);
        if (match_start) {
            int prefix_len = match_start - line;
            int match_len = strlen(pattern);
            const char *highlight_color = COLOR_CYAN;
            if (show_line_number) {
                printf("%s:%d:", filename, line_num);
            } else {
                printf("%s:", filename);
            }
            
            // 打印匹配前的部分
            printf("%.*s", prefix_len, line);
            
            // 打印带颜色的匹配部分
            printf("%s%.*s%s", highlight_color, match_len, match_start, COLOR_RESET);
            
            // 打印匹配后的部分
            printf("%s", match_start + match_len);
        } else {
            if (show_line_number) {
                printf("%s:%d:%s", filename, line_num, line);
            } else {
                printf("%s:%s", filename, line);
            }
        }
    }
}

// 检查是否为内置命令
int is_builtin(const char *cmd) {
    const char *builtins[] = {
        "ls", "cd", "cat", "grep", "echo", "history", 
        "clearhistory", "alias", "unalias", "type", NULL
    };
    
    for (int i = 0; builtins[i]; i++) {
        if (strcmp(cmd, builtins[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

// 在PATH中查找命令的绝对路径
char *find_command_in_path(const char *cmd) {
    if (strchr(cmd, '/')) {
        // 如果包含路径，直接检查文件
        if (access(cmd, X_OK) == 0) {
            return strdup(cmd);
        }
        return NULL;
    }

    char *path = getenv("PATH");
    if (!path) return NULL;

    char *path_copy = strdup(path);
    char *dir = strtok(path_copy, ":");
    char *full_path = malloc(PATH_MAX);

    while (dir) {
        snprintf(full_path, PATH_MAX, "%s/%s", dir, cmd);
        if (access(full_path, X_OK) == 0) {
            free(path_copy);
            return full_path;
        }
        dir = strtok(NULL, ":");
    }

    free(full_path);
    free(path_copy);
    return NULL;
}

// 实现type命令
void my_type(char **args) {
    if (!args[1]) {
        fprintf(stderr, "type: missing argument\n");
        return;
    }

    for (int i = 1; args[i]; i++) {
        const char *cmd = args[i];
        
        // 1. 检查是否是内置命令
        if (is_builtin(cmd)) {
            printf("%s is a shell builtin\n", cmd);
            continue;
        }
        
        // 2. 检查是否是别名
        const char *alias_cmd = resolve_alias(cmd);
        if (alias_cmd) {
            printf("%s is aliased to '%s'\n", cmd, alias_cmd);
            continue;
        }
        
        // 3. 检查是否是外部命令
        char *full_path = find_command_in_path(cmd);
        if (full_path) {
            printf("%s is %s\n", cmd, full_path);
            free(full_path);
            continue;
        }
        
        // 4. 未找到命令
        printf("%s: not found\n", cmd);
    }
}

void add_history(const char *cmd) {
    if (history_count >= HISTORY_SIZE) {
        free(history[0]);
        for (int i = 0; i < HISTORY_SIZE - 1; i++) {
            history[i] = history[i + 1];
        }
        history_count--;
    }

    history[history_count++] = strdup(cmd);

    char *path = get_history_file_path();
    FILE *fp = fopen(path, "a");
    if (fp) {
        fprintf(fp, "%s\n", cmd);
        fclose(fp);
    }
    free(path);
}

void show_history() {
    for (int i = 0; i < history_count; i++) {
        printf("%d %s\n", i + 1, history[i]);
    }
}

void clear_history() {
    for (int i = 0; i < history_count; i++) {
        free(history[i]);
    }
    history_count = 0;

    char *path = get_history_file_path();
    FILE *fp = fopen(path, "w");
    if (fp) fclose(fp);
    free(path);
    printf("History cleared\n");
}

char *get_history_file_path() {
    const char *home = getenv("HOME");
    if (!home) {
        home = getpwuid(getuid())->pw_dir;
    }
    char *path = malloc(strlen(home) + 20);
    sprintf(path, "%s/.mysh_history", home);
    return path;
}

void save_history_to_file() {
    char *path = get_history_file_path();
    FILE *fp = fopen(path, "w");
    if (!fp) return;
    for (int i = 0; i < history_count; i++) {
        fprintf(fp, "%s\n", history[i]);
    }
    fclose(fp);
    free(path);
}

void load_history_from_file() {
    char *path = get_history_file_path();
    FILE *fp = fopen(path, "r");
    if (!fp) return;

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;
        if (history_count < HISTORY_SIZE) {
            history[history_count++] = strdup(line);
        }
    }
    fclose(fp);
    free(path);
}

void add_alias(const char *name, const char *command) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            strncpy(aliases[i].command, command, sizeof(aliases[i].command) - 1);
            save_aliases_to_file();
            return;
        }
    }
    if (alias_count < MAX_ALIASES) {
        strncpy(aliases[alias_count].name, name, sizeof(aliases[alias_count].name) - 1);
        strncpy(aliases[alias_count].command, command, sizeof(aliases[alias_count].command) - 1);
        alias_count++;
        save_aliases_to_file();
    }
}

void remove_alias(const char *name) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            for (int j = i; j < alias_count - 1; j++) {
                aliases[j] = aliases[j + 1];
            }
            alias_count--;
            save_aliases_to_file();
            return;
        }
    }
    fprintf(stderr, "unalias: %s: not found\n", name);
}

void show_aliases() {
    for (int i = 0; i < alias_count; i++) {
        printf("alias %s='%s'\n", aliases[i].name, aliases[i].command);
    }
}

const char *resolve_alias(const char *name) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            return aliases[i].command;
        }
    }
    return NULL;
}

void save_aliases_to_file() {
    char path[128];
    snprintf(path, sizeof(path), "%s/.mysh_aliases", getenv("HOME"));
    FILE *fp = fopen(path, "w");
    if (!fp) return;
    for (int i = 0; i < alias_count; i++) {
        fprintf(fp, "%s='%s'\n", aliases[i].name, aliases[i].command);
    }
    fclose(fp);
}

void load_aliases_from_file() {
    char path[128];
    snprintf(path, sizeof(path), "%s/.mysh_aliases", getenv("HOME"));
    FILE *fp = fopen(path, "r");
    if (!fp) return;

    char line[300];
    while (fgets(line, sizeof(line), fp)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *name = line;
        char *cmd = eq + 1;
        cmd[strcspn(cmd, "\n")] = 0;

        if (*cmd == '\'' || *cmd == '\"') {
            cmd++;
            cmd[strlen(cmd) - 1] = '\0';
        }
        add_alias(name, cmd);
    }
    fclose(fp);
}

int run_builtin(char **args, char *raw_line) {
    // 新增：临时保存原始标准输入输出
    int saved_stdin = dup(STDIN_FILENO);
    int saved_stdout = dup(STDOUT_FILENO);
    
    // 新增：解析重定向
    char *input_file = NULL;
    char *output_file = NULL;
    parse_redirection(args, &input_file, &output_file);
    compress_args(args);
    
    // 新增：输入重定向
    if (input_file) {
        int fd = open(input_file, O_RDONLY);
        if (fd < 0) {
            perror("打开输入文件失败");
            return 1;
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    
    // 新增：输出重定向
    if (output_file) {
        int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror("创建输出文件失败");
            return 1;
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    
    // 执行内置命令
    int result = handle_builtin(args, raw_line);
    
    // 新增：恢复标准输入输出
    fflush(stdout);
    dup2(saved_stdin, STDIN_FILENO);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdin);
    close(saved_stdout);
    
    return result;
}

int handle_builtin(char **args, const char *full_line) {
    if (strcmp(args[0], "ls") == 0) my_ls(args);
    else if (strcmp(args[0], "cd") == 0) my_cd(args);
    else if (strcmp(args[0], "cat") == 0) my_cat(args);
    else if (strcmp(args[0], "grep") == 0) my_grep(args);
    else if (strcmp(args[0], "echo") == 0) my_echo(args);
    else if (strcmp(args[0], "history") == 0) {
        if (!args[1]) {
            show_history();
        } else {
            int n = atoi(args[1]);
            if (n <= 0 || n > history_count) {
                fprintf(stderr, "Invalid number for history: %s\n", args[1]);
            } else {
                for (int i = history_count - n; i < history_count; i++) {
                    printf("%d %s\n", i + 1, history[i]);
                }
            }
        }
    }
    else if (strcmp(args[0], "clearhistory") == 0) clear_history();
    else if (strcmp(args[0], "alias") == 0) {
        if (!args[1]) {
            show_aliases();
        } else {
            char temp[300];
            strncpy(temp, full_line, sizeof(temp) - 1);
            temp[sizeof(temp) - 1] = '\0';

            char *alias_body = strchr(temp, ' ');
            if (!alias_body) return 1;
            alias_body++;

            char *eq = strchr(alias_body, '=');
            if (eq) {
                *eq = '\0';
                char *name = alias_body;
                char *command = eq + 1;
                if (*command == '\'' || *command == '"') {
                    command++;
                    command[strlen(command) - 1] = '\0';
                }
                add_alias(name, command);
            } else {
                fprintf(stderr, "alias: invalid format. Usage: alias name='command'\n");
            }
        }
    } else if (strcmp(args[0], "unalias") == 0) {
        if (args[1]) {
            remove_alias(args[1]);
        } else {
            fprintf(stderr, "unalias: missing alias name\n");
        }
    } else if (strcmp(args[0], "type") == 0) {
        my_type(args);
    }
    else return 0;
    return 1;
}


