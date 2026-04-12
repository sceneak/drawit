#ifndef DS_H
#define DS_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "util.h"

#define DA_INITIAL_CAPACITY 64

#define DA_DEFINE(type, name)                                                                                   \
	struct name                                                                                             \
	{                                                                                                       \
	        size_t capacity;                                                                                \
	        size_t len;                                                                                     \
	        type elems[];                                                                                   \
	};                                                                                                      \
	NO_DISCARD static inline struct name *name##_create(size_t capacity)                                    \
	{                                                                                                       \
	        assert(capacity != 0);                                                                          \
	        struct name *alloc = (struct name *)calloc(1, sizeof(struct name) + capacity * sizeof(type));   \
	        if (!alloc)                                                                                     \
	                fputs("Allocation failure: buy more ram lol", stderr);                                  \
	        else                                                                                            \
	                alloc->capacity = capacity;                                                             \
	        return alloc;                                                                                   \
	}
	NO_DISCARD static inline struct name *name##_expand(struct name *da, int extra)                 \
	{                                                                                               \
	        struct name *temp;                                                                      \
	        int new_capacity = da->capacity;                                                        \
	                                                                                                \
	        while (da->len + extra >= new_capacity) new_capacity *= 2;                              \
	        if (new_capacity == da->capacity)                                                       \
	                return da;                                                                      \
	                                                                                                \
	        struct name *temp = realloc(da, sizeof(struct name) + new_capacity * sizeof(type);      \
	        if (!temp)                                                                              \
	        {                                                                                       \
	                fputs("Allocation failure: buy more ram lol", stderr);                          \
	                return da;                                                                      \
	        }                                                                                       \
	        da = temp;                                                                              \
	        da->capacity = new_capacity;                                                            \
	        return da;                                                                                      \
	}                                                                                                       \
	NO_DISCARD static inline struct name *name##_append(struct name *da, type elem)                         \
	{                                                                                                       \
	        name##_expand(da, 1);                                                                           \
	        da->elems[da->len-1] = elem;                                                                    \
	        return da;                                                                                      \
	}
	NO_DISCARD static inline struct name *name##_append_many(struct name *da, type elem[], int len)         \
	{                                                                                                       \
	        int i;                                                                                          \
	        name##_expand(da, len);                                                                         \
	        for (i = 0; i < len; i++)                                                                       \
	                da->elems[i] = elem[i];                                                                 \
	        return da;                                                                                      \
	}
                                                                                                                
#endif

