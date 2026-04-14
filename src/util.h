#ifndef UTIL_H
#define UTIL_H

#if defined(__GNUC__) || defined(__clang__)
	#define NO_DISCARD __attribute__((warn_unused_result))
#elif __STDC_VERSION__ >= 202311L
	#define NO_DISCARD [[nodiscard]]
#else
	#define NO_DISCARD
#endif

#define RINGBUF_INCR(idx, len, n) ( ((idx)+(n)) % (len) )
#define RINGBUF_DECR(idx, len, n) ( ((idx)+ (len)-((n) % (len))) % (len) )

#define _CAT(a, b) a##b
#define CAT(a, b) _CAT(a, b)

#endif
