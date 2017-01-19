#ifndef NETDATA_INLINED_H
#define NETDATA_INLINED_H

#include "common.h"

// for faster execution, allow the compiler to inline
// these functions that are called thousands of times per second

static inline uint32_t simple_hash(const char *name) {
    register unsigned char *s = (unsigned char *) name;
    register uint32_t hval = 0x811c9dc5;
    while (*s) {
        hval *= 16777619;
        hval ^= (uint32_t) *s++;
    }
    return hval;
}

static inline uint32_t simple_uhash(const char *name) {
    register unsigned char *s = (unsigned char *) name;
    register uint32_t hval = 0x811c9dc5, c;
    while ((c = *s++)) {
        if (unlikely(c >= 'A' && c <= 'Z')) c += 'a' - 'A';
        hval *= 16777619;
        hval ^= c;
    }
    return hval;
}

static inline int str2i(const char *s) {
    register int n = 0;
    register char c, negative = (*s == '-');

    for(c = (negative)?*(++s):*s; c >= '0' && c <= '9' ; c = *(++s)) {
        n *= 10;
        n += c - '0';
    }

    if(unlikely(negative))
        return -n;

    return n;
}

static inline long str2l(const char *s) {
    register long n = 0;
    register char c, negative = (*s == '-');

    for(c = (negative)?*(++s):*s; c >= '0' && c <= '9' ; c = *(++s)) {
        n *= 10;
        n += c - '0';
    }

    if(unlikely(negative))
        return -n;

    return n;
}

static inline unsigned long str2ul(const char *s) {
    register unsigned long n = 0;
    register char c;
    for(c = *s; c >= '0' && c <= '9' ; c = *(++s)) {
        n *= 10;
        n += c - '0';
    }
    return n;
}

static inline unsigned long long str2ull(const char *s) {
    register unsigned long long n = 0;
    register char c;
    for(c = *s; c >= '0' && c <= '9' ; c = *(++s)) {
        n *= 10;
        n += c - '0';
    }
    return n;
}

static inline int read_single_number_file(const char *filename, unsigned long long *result) {
    char buffer[1024 + 1];

    int fd = open(filename, O_RDONLY, 0666);
    if(unlikely(fd == -1)) {
        *result = 0;
        return 1;
    }

    ssize_t r = read(fd, buffer, 1024);
    if(unlikely(r == -1)) {
        *result = 0;
        close(fd);
        return 2;
    }

    close(fd);
    *result = str2ull(buffer);
    return 0;
}

#endif //NETDATA_INLINED_H
