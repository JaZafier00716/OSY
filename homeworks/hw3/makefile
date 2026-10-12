CCFLAGS = -Wall -Wextra -Werror -pedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer 
CC = gcc

all: main
	
test: main
	./main soubor.txt

clean:
	rm -f main

main: main.cpp
	clear
	$(CC) $(CCFLAGS) $^ -o $@