CFLAGS = -g -Wall -Wextra -Werror
LDFLAGS = -L
LDLIBS = -l
CC = gcc

.PHONY: all clean test

all: main1/target1 main2/target2 strtransform/strtransform
# 	@echo "All targets built successfully."
# 	LD_LIBRARY_PATH=./dyn_lib1
# 	./main1/target1 100 20 > soubor1.txt
# 	./main2/target2 < soubor1.txt > soubor2.txt
# 	./main1/target1 100 30 | ./main2/target2 > soubor3.txt
# 	LD_LIBRARY_PATH=./dyn_lib2
# 	./main2/target2 < soubor2.txt
# 	./main2/target2 < soubor3.txt

clean:
	@echo "Cleaning up..."
	rm -f \
		static_lib/generation.o \
		static_lib/libgeneration.a \
		charutil/charutil.o \
		charutil/libcharutil.a \
		upper_lib/libtransform.so \
		lower_lib/libtransform.so \
		swap_lib/libtransform.so \
		final \
		main1/main1.o \
		main2/main2.o \
		main1/target1 \
		main2/target2 \
		dyb_lib1/libcompute.so \
		dyn_lib2/libcompute.so\
		strtransform/strtransform


test: strtransform/strtransform
	@set -eu; \
	input=$$(mktemp); \
	upper_expected=$$(mktemp); \
	lower_expected=$$(mktemp); \
	swap_expected=$$(mktemp); \
	trap 'rm -f "$$input" "$$upper_expected" "$$lower_expected" "$$swap_expected"' EXIT; \
	printf '%s\n' \
		'Hello World' \
		'AbC xYz' \
		'123 !@# a1Z' \
		'already lower' \
		'ALREADY UPPER' \
		'Mixed-Case_42' \
		'' > "$$input"; \
	printf '%s\n' \
		'HELLO WORLD' \
		'ABC XYZ' \
		'123 !@# A1Z' \
		'ALREADY LOWER' \
		'ALREADY UPPER' \
		'MIXED-CASE_42' \
		'' > "$$upper_expected"; \
	printf '%s\n' \
		'hello world' \
		'abc xyz' \
		'123 !@# a1z' \
		'already lower' \
		'already upper' \
		'mixed-case_42' \
		'' > "$$lower_expected"; \
	printf '%s\n' \
		'hELLO wORLD' \
		'aBc XyZ' \
		'123 !@# A1z' \
		'ALREADY LOWER' \
		'already upper' \
		'mIXED-cASE_42' \
		'' > "$$swap_expected"; \
	status=0; \
	run_test() { \
		name=$$1; \
		library=$$2; \
		expected=$$3; \
		actual=$$(mktemp); \
		if LD_LIBRARY_PATH="./$$library" ./strtransform/strtransform < "$$input" > "$$actual" && diff -u "$$expected" "$$actual"; then \
			echo "$$name: PASS"; \
		else \
			echo "$$name: FAIL"; \
			status=1; \
		fi; \
		rm -f "$$actual"; \
	}; \
	run_test upper upper_lib "$$upper_expected"; \
	run_test lower lower_lib "$$lower_expected"; \
	run_test swap swap_lib "$$swap_expected"; \
	exit $$status

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


charutil/charutil.o: charutil/charutil.c charutil/charutil.h
	@echo "Compiling charutil/charutil.c..."
	$(CC) $(CFLAGS) -fPIC -c charutil/charutil.c -o charutil/charutil.o


charutil/libcharutil.a: charutil/charutil.o
	@echo "Creating static library charutil/libcharutil.a..."
	ar rcs charutil/libcharutil.a charutil/charutil.o

upper_lib/libtransform.so: upper_lib/transform.c upper_lib/transform.h charutil/libcharutil.a
	@echo "Compiling upper_lib/transform.c..."
	$(CC) $(CFLAGS) -fPIC -shared -I./charutil upper_lib/transform.c charutil/libcharutil.a -o $@


lower_lib/libtransform.so: lower_lib/transform.c lower_lib/transform.h charutil/libcharutil.a
	@echo "Compiling lower_lib/transform.c..."
	$(CC) $(CFLAGS) -fPIC -shared -I./charutil lower_lib/transform.c charutil/libcharutil.a -o $@

swap_lib/libtransform.so: swap_lib/transform.c swap_lib/transform.h charutil/libcharutil.a
	@echo "Compiling swap_lib/transform.c..."
	$(CC) $(CFLAGS) -fPIC -shared -I./charutil swap_lib/transform.c charutil/libcharutil.a -o $@



strtransform/strtransform: strtransform/strtransform.c lower_lib/libtransform.so upper_lib/libtransform.so swap_lib/libtransform.so
	$(CC) $(CFLAGS) -I./lower_lib strtransform/strtransform.c -o $@ -L./lower_lib -ltransform
