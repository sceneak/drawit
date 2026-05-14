#ifndef UTIL_H
#define UTIL_H

#include <ctype.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
	#define NO_DISCARD __attribute__((warn_unused_result))
#elif __STDC_VERSION__ >= 202311L
	#define NO_DISCARD [[nodiscard]]
#else
	#define NO_DISCARD
#endif

#define RINGBUF_INCR(idx, count, n) ( ((idx)+(n)) % (count) )
#define RINGBUF_DECR(idx, count, n) ( ((idx)+ (count)-((n) % (count))) % (count) )

#define _CAT(a, b) a##b
#define CAT(a, b) _CAT(a, b)

#define min(a, b) ((a) < (b) ? (a) : (b))

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

static inline int drawit_strcasecmp(const char *s1, const char *s2)
{
    while (*s1 && (tolower((unsigned char)*s1) == tolower((unsigned char)*s2))) {
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

#endif
