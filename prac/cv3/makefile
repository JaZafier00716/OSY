CCFLAGS = -Wall -Wextra -Werror -pedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer 
CC = gcc

all: fileinfo/fileinfo followfile/followfile

clean:
	rm -f 	fileinfo/fileinfo \
			fileinfo/fileinfo.o \
			fileinfo/libfileinfo.a \
			followfile/followfile

fileinfo/fileinfo.o: fileinfo/fileinfo.c fileinfo/fileinfo.h
	$(CC) $(CCFLAGS) -c fileinfo/fileinfo.c -o fileinfo/fileinfo.o -Ifileinfo

fileinfo/libfileinfo.a: fileinfo/fileinfo.o
	ar rcs fileinfo/libfileinfo.a fileinfo/fileinfo.o

fileinfo/fileinfo: info_main.c fileinfo/libfileinfo.a
	$(CC) $(CCFLAGS) info_main.c -Lfileinfo -lfileinfo -o fileinfo/fileinfo -Ifileinfo

followfile/followfile: follow_main.c
	$(CC) $(CCFLAGS) follow_main.c -o followfile/followfile