
all: main.c
	gcc -o db main.c

test:
	bundle exec rspec

clean:
	rm db
