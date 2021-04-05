all: build

build: so_stdio.o utils.o
	gcc -shared so_stdio.o utils.o -o libso_stdio.so -Wall -g

so_stdio.o: so_stdio.c
	gcc -Wall -fPIC -g so_stdio.c -c -o so_stdio.o

utils.o: utils.c
	gcc -Wall -fPIC -g utils.c -c -o utils.o

clean:
	rm *.o libso_stdio.so
