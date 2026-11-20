#ifndef T
	#define T void*
	#error "T (Type) not defined"
#endif
#ifndef NAME
	#define NAME da_unknown
	#error "NAME not defined"
#endif

#ifndef DA_T_H
	#include <stdlib.h>
	#include <assert.h>
	#include <stdio.h>
	#include "util.h"

	#define DA_INITIAL_CAPACITY 64
	#define DA_LAST(da) ((da)->elems + (da)->count-1)
#endif

struct NAME
{
	size_t capacity;
	size_t count;
	T elems[];
};

#define NAME_CREATE CAT(NAME, _create)
NO_DISCARD static inline struct NAME *NAME_CREATE(size_t capacity)
{
	assert(capacity != 0);
	struct NAME *alloc = (struct NAME *)calloc(1, sizeof(struct NAME) + capacity * sizeof(T));
	if (!alloc)
		fputs("Allocation failure: buy more ram lol", stderr);
	else
		alloc->capacity = capacity;
	return alloc;
}

#define NAME_APPEND_EMPTY CAT(NAME, _append_empty)
NO_DISCARD static inline struct NAME *NAME_APPEND_EMPTY(struct NAME *da, int extra)
{
	struct NAME *temp;
	size_t new_capacity = da->capacity;

	while (da->count + extra >= new_capacity) new_capacity *= 2;
	if (new_capacity == da->capacity)
	{
		da->count += extra;
		return da;
	}

	temp = (struct NAME *)realloc(da, sizeof(struct NAME) + new_capacity * sizeof(T));
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

#define NAME_APPEND CAT(NAME,_append)
NO_DISCARD static inline struct NAME *NAME_APPEND(struct NAME *da, T elem)
{
	da = CAT(NAME, _append_empty)(da, 1);
	da->elems[da->count-1] = elem;
	return da;
}

#define NAME_APPEND_N CAT(NAME,_append_n)
NO_DISCARD static inline struct NAME *NAME_APPEND_N(struct NAME *da, T const elem[], int n)
{
	int i;
	da = CAT(NAME, _append_empty)(da, n);
	for (i = 0; i < n; i++)
		da->elems[da->count - n + i] = elem[i];
	return da;
}


#define NAME_SWPPOP CAT(NAME,_swppop)
NO_DISCARD static inline struct NAME *NAME_SWPPOP(struct NAME *da, size_t i)
{
	da->elems[i] = da->elems[--da->count];
	return da;
}

#define NAME_SFTPOP CAT(NAME,_sftpop)
NO_DISCARD static inline struct NAME *NAME_SFTPOP(struct NAME *da, size_t i)
{
	size_t j;

	assert(da->count);

	for (j = 0; j+1 < da->count; j++) { 
		if (j < i) continue;
		da->elems[j] = da->elems[j+1];
	}
	da->count--;
	return da;
}

#define NAME_DESTROY CAT(NAME,_destroy)
NO_DISCARD static inline struct NAME *NAME_DESTROY(struct NAME *da)
{
	free(da);
	return NULL;
}

#undef T
#undef NAME
