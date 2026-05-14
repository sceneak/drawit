#include <stdint.h>

#define GAPBUF_INITIAL_ALLOC 512

#define COLOR_INIT_HEX(hex) {             \
               .r = ((hex) >> 24) & 0xFF, \
               .g = ((hex) >> 16) & 0xFF, \
               .b = ((hex) >> 8) & 0xFF,  \
               .a =  (hex) & 0xFF,        \
        }
#define COLOR_FROM_HEX(hex) (color) COLOR_INIT_HEX(hex)

#define BOUNDS_INIT {    \
                .x0 = INFINITY,  \
                .y0 = INFINITY,  \
                .x1 = -INFINITY, \
                .y1 = -INFINITY, \
        }

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef struct { float x, y; } vec2;
typedef struct { float x, y, z; } vec3;
typedef struct { unsigned char r, g, b, a; } color;
typedef struct { float x0, y0, x1, y1; } rect;

struct gapbuf {	
	size_t gap_start, 
	       gap_end,
	       capacity;
	char data[];
};

static inline vec2 vec2_all(float s)
{
	return (vec2){s, s};
}

static inline float vec2_dist2(vec2 from, vec2 to)
{
	vec2 dist = { to.x - from.x, to.y - from.y };
	return dist.x * dist.x + dist.y * dist.y; 
}


static inline rect rect_create(vec2 center, vec2 extents)
{
	return (rect){
		.x0 = center.x - extents.x,
		.y0 = center.y - extents.y,
		.x1 = center.x + extents.x,
		.y1 = center.y + extents.y,
	};
}

static inline rect rect_fit(rect r, vec2 v)
{
	return (rect){
		.x0 = fminf(r.x0, v.x),
		.y0 = fminf(r.y0, v.y),
		.x1 = fmaxf(r.x1, v.x),
		.y1 = fmaxf(r.y1, v.y),
	};
}

static inline rect rect_fit_rect(rect r1, rect r2)
{
	return (rect){
		.x0 = fminf(r1.x0, r2.x0),
		.y0 = fminf(r1.y0, r2.y0),
		.x1 = fmaxf(r1.x1, r2.x1),
		.y1 = fmaxf(r1.y1, r2.y1),
	};
}

static inline bool rect_contains(rect r, vec2 v)
{
	return r.x0 < v.x && v.x < r.x1
	    && r.y0 < v.y && v.y < r.y1;
}


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

static inline int gapbuf_to_actual(const struct gapbuf *buf, size_t i)
{
	return i < buf->gap_start ? i : buf->gap_end + i - buf->gap_start;
}

static inline char gapbuf_at(const struct gapbuf *buf, size_t i)
{
	return buf->data[gapbuf_to_actual(buf, i)];
}


