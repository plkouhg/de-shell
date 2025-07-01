all: de-shell

de-shell: main.c builtin.c input.c
	gcc -o de-shell main.c builtin.c input.c;

clean:
	rm -f de-shell;
