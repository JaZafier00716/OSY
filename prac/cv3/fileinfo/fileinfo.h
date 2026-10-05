#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <stdbool.h>
#include <string.h>

void print_file_type(mode_t mode);
void print_file_permissions(mode_t read, mode_t write, mode_t execute);

int print_file_info(const char *filename, bool symlink);