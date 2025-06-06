#include "lib/string.h"



size_t strlen(const char *str) {
    size_t i = 0;
    while(str[i] != '\0') ++i;
    return i;
}

int strcmp(const char *lhs, const char *rhs) {
    size_t i = 0;
    while(lhs[i] == rhs[i])
        if(lhs[i++] == '\0') return 0;
    return lhs[i] < rhs[i] ? -1 : 1;
}

bool streq(const char *lhs, const char *rhs) {
    return strcmp(lhs, rhs) == 0;
}

char *strcpy(char *dest, const char *src) {
    size_t i;
    for(i = 0; src[i] != '\0'; i++) dest[i] = src[i];
    dest[i] = src[i];
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for(i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for(; i < n; i++) dest[i] = '\0';
    return dest;
}