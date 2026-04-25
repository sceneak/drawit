#ifndef T
	#define T void*
#endif
#ifndef name
	#define name da_unknown
#endif

#ifndef DA_T_H
	#include <stdlib.h>
	#include <assert.h>
	#include <stdio.h>
	#include "util.h"

	#define DA_INITIAL_CAPACITY 64
#endif

struct name
{
	size_t capacity;
	size_t count;
	T elems[];
};

NO_DISCARD static inline struct name *CAT(name, _create)(size_t capacity)
{
	assert(capacity != 0);
	struct name *alloc = (struct name *)calloc(1, sizeof(struct name) + capacity * sizeof(T));
	if (!alloc)
		fputs("Allocation failure: buy more ram lol", stderr);
	else
		alloc->capacity = capacity;
	return alloc;
}
NO_DISCARD static inline struct name *CAT(name, _append_empty)(struct name *da, int extra)
{
	struct name *temp;
	int new_capacity = da->capacity;

	while (da->count + extra >= new_capacity) new_capacity *= 2;
	if (new_capacity == da->capacity)
	{
		da->count += extra;
		return da;
	}

	temp = (struct name *)realloc(da, sizeof(struct name) + new_capacity * sizeof(T));
	if (!temp)
	{
		fputs("Allocation failure: buy more ram lol", stderr);
		return NULL;
	}
	da = temp;
	da->count += extra;
	da->capacity = new_capacity;
	return da;
}
NO_DISCARD static inline struct name *CAT(name, _append)(struct name *da, T elem)
{
	da = CAT(name, _append_empty)(da, 1);
	da->elems[da->count-1] = elem;
	return da;
}
NO_DISCARD static inline struct name *CAT(name, _append_n)(struct name *da, T elem[], int n)
{
	int i;
	da = CAT(name, _append_empty)(da, n);
	for (i = 0; i < n; i++)
		da->elems[da->count - n + i] = elem[i];
	return da;
}

#undef T
#undef name
