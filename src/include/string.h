#ifndef STRING_H
#define STRING_H

typedef unsigned long size_t;

size_t strlen(char const *s);
size_t strnlen(char const *s, size_t count);
void *memcpy(void *dest, const void *src, size_t count);
void *memset(void *s, int c, size_t count);
void *memmove(void *dest, const void *src, size_t count);
int memcmp(const void *cs, const void *ct, size_t count);
void *memchr(void const *buf, int c, size_t len);
char *strrchr(char const *s, int c);

#endif
