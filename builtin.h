#ifndef BUILTIN_H
#define BUILTIN_H

#include <stdio.h>
#include <ctype.h>

#define HISTORY_SIZE 100
#define MAX_ALIASES 100

typedef struct {
    char name[64];
    char command[256];
} Alias;

extern Alias aliases[MAX_ALIASES];
extern int alias_count;

int handle_builtin(char **args, const char *full_line);
void my_cd(char **args);
void my_echo(char **args);
void my_ls(char **args);
void my_cat(char **args);
void my_grep(char **args);
void add_history(const char *cmd);
void show_history();
void clear_history();
void load_history_from_file();
void save_history_to_file();
char *get_history_file_path();
void show_prompt();

// alias 功能
void add_alias(const char *name, const char *command);
void show_aliases();
void remove_alias(const char *name);
const char *resolve_alias(const char *name);
void save_aliases_to_file();
void load_aliases_from_file();

#endif
