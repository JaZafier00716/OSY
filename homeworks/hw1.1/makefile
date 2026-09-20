CFLAGS = -g -Wall -Wextra -Werror
LDFLAGS = -L
LDLIBS = -l
CC = gcc

all: main1/target1 main2/target2
	@echo "All targets built successfully."
	LD_LIBRARY_PATH=./dyn_lib1
	./main1/target1 100 20 > soubor1.txt
	./main2/target2 < soubor1.txt > soubor2.txt
	./main1/target1 100 30 | ./main2/target2 > soubor3.txt
	LD_LIBRARY_PATH=./dyn_lib2
	./main2/target2 < soubor2.txt
	./main2/target2 < soubor3.txt

clean:
	@echo "Cleaning up..."
	rm -f static_lib/generation.o static_lib/libgeneration.a final main1/main1.o main2/main2.o main1/target1 main2/target2 dyb_lib1/libcompute.so dyn_lib2/libcompute.so

static_lib/generation.o: static_lib/generation.c static_lib/generation.h
	@echo "Compiling static_lib/generation.c..."
	$(CC) $(CFLAGS) -c static_lib/generation.c -o static_lib/generation.o

static_lib/libgeneration.a: static_lib/generation.o
	@echo "Creating static library static_lib/libgeneration.a..."
# 	$@ represents the target name (static_lib/libgeneration.a)
# 	$^ represents all the prerequisites (static_lib/generation.o)
	ar rcs $@ $^


main1/target1: main1/main1.c static_lib/libgeneration.a
	@echo "Linking main1/target1 executable..."
# 	-I<header_path> specifies the directory to search for header files
# 	-L<library_path> specifies the directory to search for libraries
# 	-l<library_name> specifies the library to link against (without the 'lib' prefix and '.a' suffix)
	$(CC) $(CFLAGS) -I./static_lib main1/main1.c -o main1/target1 -L./static_lib -lgeneration


dyn_lib1/libcompute.so: dyn_lib1/compute.c dyn_lib1/compute.h
	@echo "Creating dynamic library dyn_lib1/libcompute.so..."
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $^


dyn_lib2/libcompute.so: dyn_lib2/compute.c dyn_lib2/compute.h
	@echo "Creating dynamic library dyn_lib2/libcompute.so..."
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $^

main2/target2: main2/main2.c dyn_lib1/libcompute.so dyn_lib2/libcompute.so
	@echo "Linking final executable for main2/target2..."
	$(CC) $(CFLAGS) -I./dyn_lib1 -L./dyn_lib1 -lcompute $^ -o $@