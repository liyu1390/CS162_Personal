/*
 * mm_alloc.h
 *
 * A clone of the interface documented in "man 3 malloc".
 */

#pragma once

#include <stdlib.h>
#include <string.h>

void *mm_malloc(size_t size);
void *mm_realloc(void *ptr, size_t size);
void mm_free(void *ptr);

struct block{
	struct block *prev;
	struct block *next;
	int free;
	int size;
};

void zeroData(struct block *currentMeta);