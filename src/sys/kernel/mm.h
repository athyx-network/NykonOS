#ifndef MM_H
#define MM_H

#include <stdint.h>
#include <stddef.h>

void mm_init();
void *nykon_malloc(unsigned int size);
void nykon_free(void *ptr);
unsigned int mm_get_total_bytes(void);
unsigned int mm_get_used_bytes(void);
unsigned int mm_get_free_bytes(void);

#endif
