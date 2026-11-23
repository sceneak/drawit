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
static inline void NAME_CREATE(struct NAME **da, size_t capacity)
{
	assert(capacity != 0);
	*da = (struct NAME *)calloc(1, sizeof(struct NAME) + capacity * sizeof(T));
	if (!*da)
		fputs("Allocation failure: buy more ram lol", stderr);
	else
		(*da)->capacity = capacity;
}

#define NAME_APPEND_EMPTY CAT(NAME, _append_empty)
static inline void NAME_APPEND_EMPTY(struct NAME **da, int extra)
{
	struct NAME *temp;
	size_t new_capacity = (*da)->capacity;

	while ((*da)->count + extra >= new_capacity) new_capacity *= 2;
	if (new_capacity == (*da)->capacity)
	{
		(*da)->count += extra;
		return;
	}

	temp = (struct NAME *)realloc(*da, sizeof(struct NAME) + new_capacity * sizeof(T));
	if (!temp)
	{
		fputs("Allocation failure: buy more ram lol", stderr);
		*da = NULL;
		return;
	}
	*da = temp;
	(*da)->count += extra;
	(*da)->capacity = new_capacity;
}

#define NAME_APPEND CAT(NAME,_append)
static inline void NAME_APPEND(struct NAME **da, T elem)
{
	NAME_APPEND_EMPTY(da, 1);
	(*da)->elems[(*da)->count-1] = elem;
}

#define NAME_APPEND_N CAT(NAME,_append_n)
static inline void NAME_APPEND_N(struct NAME **da, T const elem[], int n)
{
	int i;
	NAME_APPEND_EMPTY(da, n);
	for (i = 0; i < n; i++)
		(*da)->elems[(*da)->count - n + i] = elem[i];
}


#define NAME_SWPPOP CAT(NAME,_swppop)
static inline void NAME_SWPPOP(struct NAME **da, size_t i)
{
	(*da)->elems[i] = (*da)->elems[--(*da)->count];
}

#define NAME_SFTPOP CAT(NAME,_sftpop)
static inline void NAME_SFTPOP(struct NAME **da, size_t i)
{
	size_t j;

	assert((*da)->count);

	for (j = 0; j+1 < (*da)->count; j++) { 
		if (j < i) continue;
		(*da)->elems[j] = (*da)->elems[j+1];
	}
	(*da)->count--;
}

#define NAME_DESTROY CAT(NAME,_destroy)
static inline void NAME_DESTROY(struct NAME **da)
{
	free(*da);
	*da = NULL;
}

#define NAME_ELEMDUP CAT(NAME,_elemdup)
static inline T *NAME_ELEMDUP(const struct NAME *da)
{
	T *res;

	if (!da->count) return NULL;

	res = malloc(da->count * sizeof(T));
	if (!res) return NULL;

	memcpy(res, da->elems, da->count * sizeof(T));
	return res;
}

#undef T
#undef NAME
