all: compile run clean

compile:
	gcc -o shell seashell.c

run:
	./shell

clean:
	rm -rf shell
