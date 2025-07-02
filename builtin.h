#ifndef BUILTIN_H
#define BUILTIN_H

#include <stdio.h>
#include <ctype.h>
#include <regex.h>
#include <limits.h>

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
void my_type(char **args);
void add_history(const char *cmd);
void show_history();
void clear_history();
void load_history_from_file();
void save_history_to_file();
char *get_history_file_path();
void show_prompt();
//grep功能
void process_file_or_dir(const char *path, const char *pattern, regex_t *regex,
                        int ignore_case, int invert_match, int line_number,
                        int count_only, int recursive, int files_with_matches,
                        int only_matching, int extended_regex,
                        int after_context, int before_context, int context_lines);

void process_directory(const char *dirpath, const char *pattern, regex_t *regex,
                      int ignore_case, int invert_match, int line_number,
                      int count_only, int files_with_matches,
                      int only_matching, int extended_regex,
                      int after_context, int before_context, int context_lines);

void process_file(const char *filename, const char *pattern, regex_t *regex,
                 int ignore_case, int invert_match, int line_number,
                 int count_only, int files_with_matches,
                 int only_matching, int extended_regex,
                 int after_context, int before_context, int context_lines);

void print_line(const char *filename, int line_num, const char *line, 
               int show_line_number, int only_matching, char *pattern);
// alias 功能
void add_alias(const char *name, const char *command);
void show_aliases();
void remove_alias(const char *name);
const char *resolve_alias(const char *name);
void save_aliases_to_file();
void load_aliases_from_file();
int is_builtin(const char *cmd);
char *find_command_in_path(const char *cmd);
#endif
