// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2024 Nikita Travkin <nikita@trvn.ru> */

/*
 * Most things taken from lk libc:
 * Copyright (c) 2008 Travis Geiselbrecht
 */

#include <efi.h>
#include <efilib.h>

#include <string.h>

size_t strlen(char const *s)
{
	size_t i = 0;

	while (s[i])
		i++;

	return i;
}

size_t strnlen(char const *s, size_t count)
{
	const char *sc;

	for(sc = s; count-- && *sc != '\0'; ++sc)
		;
	return sc - s;
}

int strncmp(char const *cs, char const *ct, size_t count)
{
	signed char __res = 0;

	while(count > 0) {
		if ((__res = *cs - *ct++) != 0 || !*cs++)
			break;
		count--;
	}

	return __res;
}


/* // in gnu-efi
void *memcpy(void *dest, const void *src, size_t count)
{
	CopyMem(dest, (void*)src, count);
	return dest;
}

void *memset(void *s, int c, size_t count)
{
	SetMem(s, count, c);
	return s;
}
*/

typedef long word;

#define lsize sizeof(word)
#define lmask (lsize - 1)

void *
memmove(void *dest, void const *src, size_t count)
{
	char *d = (char *)dest;
	const char *s = (const char *)src;
	int len;

	if(count == 0 || dest == src)
		return dest;

	if((long)d < (long)s) {
		if(((long)d | (long)s) & lmask) {
			// src and/or dest do not align on word boundary
			if((((long)d ^ (long)s) & lmask) || (count < lsize))
				len = count; // copy the rest of the buffer with the byte mover
			else
				len = lsize - ((long)d & lmask); // move the ptrs up to a word boundary

			count -= len;
			for(; len > 0; len--)
				*d++ = *s++;
		}
		for(len = count / lsize; len > 0; len--) {
			*(word *)d = *(word *)s;
			d += lsize;
			s += lsize;
		}
		for(len = count & lmask; len > 0; len--)
			*d++ = *s++;
	} else {
		d += count;
		s += count;
		if(((long)d | (long)s) & lmask) {
			// src and/or dest do not align on word boundary
			if((((long)d ^ (long)s) & lmask) || (count <= lsize))
				len = count;
			else
				len = ((long)d & lmask);

			count -= len;
			for(; len > 0; len--)
				*--d = *--s;
		}
		for(len = count / lsize; len > 0; len--) {
			d -= lsize;
			s -= lsize;
			*(word *)d = *(word *)s;
		}
		for(len = count & lmask; len > 0; len--)
			*--d = *--s;
	}

	return dest;
}

int memcmp(const void *cs, const void *ct, size_t count)
{
	const unsigned char *su1, *su2;
	signed char res = 0;

	for(su1 = cs, su2 = ct; 0 < count; ++su1, ++su2, count--)
		if((res = *su1 - *su2) != 0)
			break;
	return res;
}

void *memchr(void const *buf, int c, size_t len)
{
	size_t i;
	unsigned char const *b= buf;
	unsigned char        x= (c&0xff);

	for(i= 0; i< len; i++) {
		if(b[i]== x) {
			return (void*)(b+i);
		}
	}

	return NULL;
}

char *strrchr(char const *s, int c)
{
	char const *last= c?0:s;


	while(*s) {
		if(*s== c) {
			last= s;
		}

		s+= 1;
	}

	return (char *)last;
}

char *strchr(const char *s, int c)
{
	for(; *s != (char) c; ++s)
		if (*s == '\0')
			return NULL;
	return (char *) s;
}

int isspace(int c)
{
	return (c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' || c == '\v');
}

#define LONG_MAX __LONG_MAX__
#define ULONG_MAX (LONG_MAX * 2UL + 1UL)

unsigned long strtoul(const char *nptr, char **endptr, int base) {
	int neg = 0;
	unsigned long ret = 0;

	if (base < 0 || base == 1 || base > 36) {
		return 0;
	}

	while (isspace(*nptr)) {
		nptr++;
	}

	if (*nptr == '+') {
		nptr++;
	} else if (*nptr == '-') {
		neg = 1;
		nptr++;
	}

	if ((base == 0 || base == 16) && nptr[0] == '0' && nptr[1] == 'x') {
		base = 16;
		nptr += 2;
	} else if (base == 0 && nptr[0] == '0') {
		base = 8;
		nptr++;
	} else if (base == 0) {
		base = 10;
	}

	for (;;) {
		char c = *nptr;
		int v = -1;
		unsigned long new_ret;

		if (c >= 'A' && c <= 'Z') {
			v = c - 'A' + 10;
		} else if (c >= 'a' && c <= 'z') {
			v = c - 'a' + 10;
		} else if (c >= '0' && c <= '9') {
			v = c - '0';
		}

		if (v < 0 || v >= base) {
			*endptr = (char *) nptr;
			break;
		}

		new_ret = ret * base;
		if (new_ret / base != ret ||
		    new_ret + v < new_ret ||
		    ret == ULONG_MAX) {
			ret = ULONG_MAX;
		} else {
			ret = new_ret + v;
		}

		nptr++;
	}

	if (neg && ret != ULONG_MAX) {
		ret = -ret;
	}

	return ret;
}

