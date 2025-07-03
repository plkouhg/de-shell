#ifndef INPUT_H
#define INPUT_H

char *read_input_line();
void filter_and_add_history(const char *cmd);
char **expand_args(char **args);

#endif  
