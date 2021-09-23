
all: main.c
	gcc -o db main.c constants.c node.c table.c pager.c

test:
	bundle exec rspec

clean:
	rm -f db test.db
