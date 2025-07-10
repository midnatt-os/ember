#pragma once

#include <stddef.h>

void* copy_string_from_user(char* src, size_t length);

void syscall_init();
