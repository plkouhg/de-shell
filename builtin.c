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

#define MAX_LINE 1024

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
            printf("%s ", args[i]);
        }
    }
    printf("\n");
}

void my_grep(char **args) {
    int ignore_case = 0;
    int pattern_index = 1;

    if (args[1] && strcmp(args[1], "-i") == 0) {
        ignore_case = 1;
        pattern_index = 2;
    }

    if (!args[pattern_index] || !args[pattern_index + 1]) {
        fprintf(stderr, "Usage: grep [-i] pattern file\n");
        return;
    }

    char *pattern = args[pattern_index];
    char *filename = args[pattern_index + 1];

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror(filename);
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        if ((ignore_case && strcasestr(line, pattern)) || (!ignore_case && strstr(line, pattern))) {
            printf("%s", line);
        }
    }

    fclose(fp);
}

void my_type(char **args) {
    if (!args[1]) {
        fprintf(stderr, "type: missing argument\n");
        return;
    }
    printf("%s is a shell builtin\n", args[1]);
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
    } else return 0;
    return 1;
}


