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

#define min(a, b) ( (a) < (b) ? (a) : (b) )

#define GAPBUF_INITIAL_ALLOC 512

static inline int drawit_strcasecmp(const char *s1, const char *s2)
{
    while (*s1 && (tolower((unsigned char)*s1) == tolower((unsigned char)*s2))) {
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

struct gapbuf {	
	size_t gap_start, 
	       gap_end,
	       capacity;
	char data[];
};

static inline struct gapbuf *gapbuf_create(size_t capacity)
{
	struct gapbuf *buf = (struct gapbuf *)calloc(1, sizeof(struct gapbuf) + capacity);
	buf->capacity = capacity;
	buf->gap_end = capacity;
	return buf;
}

static inline size_t gapbuf_suffix_len(const struct gapbuf *buf)
{
	return buf->capacity - buf->gap_end;
}

static inline size_t gapbuf_len(const struct gapbuf *buf)
{
	return buf->gap_start + gapbuf_suffix_len(buf);
}

static inline bool gapbuf_open(struct gapbuf *buf, size_t idx)
{
	size_t delta;

	if (idx > gapbuf_len(buf))
		return false;

	if (buf->gap_start == idx)
		return true;

	if (idx < buf->gap_start) {
		delta = buf->gap_start - idx; 

		buf->gap_start -= delta;
		buf->gap_end -= delta;
		memmove(buf->data + buf->gap_end, buf->data + buf->gap_start, delta);
	} else {
		delta = idx - buf->gap_start; 

		memmove(buf->data + buf->gap_start, buf->data + buf->gap_end, delta);
		buf->gap_start += delta;
		buf->gap_end += delta;
	}

	return true;
}

static inline int gapbuf_grow(struct gapbuf **buf_ptr, size_t new_capacity)
{
	struct gapbuf *buf = *buf_ptr;
	size_t new_gap_end = new_capacity - gapbuf_suffix_len(buf);

	buf = realloc(buf, sizeof(struct gapbuf) + new_capacity);
	if (!buf) {
		fputs("Allocation failure: buy more ram lol", stderr);
		return -1;
	}
	
	if (buf->gap_end < buf->capacity)
		memmove(buf->data + new_gap_end, buf->data + buf->gap_end, gapbuf_suffix_len(buf));

	buf->capacity = new_capacity;
	buf->gap_end = new_gap_end;

	*buf_ptr = buf;
	return 0;
}

static inline void gapbuf_insert(struct gapbuf **buf_ptr, char c)
{
	struct gapbuf *buf = *buf_ptr;

	if (buf->gap_start >= buf->gap_end)
		gapbuf_grow(&buf, buf->capacity*2);
	buf->data[buf->gap_start++] = c;

	*buf_ptr = buf;
}

static inline void gapbuf_delete(struct gapbuf *buf)
{
	if (buf->gap_start > 0)
		buf->gap_start--;
}

static inline char gapbuf_at(const struct gapbuf *buf, size_t i)
{
	if (i < buf->gap_start)
		return buf->data[i];
	else
		return buf->data[buf->gap_end + i - buf->gap_start];

}

#endif
