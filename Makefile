
all: main.c
	gcc -o db main.c

clean:
	rm db
