#include "mm.h"

// Define the heap space: 8 MB starting at 0x00600000 on bare-metal, or an array in Linux
#define HEAP_SIZE (8 * 1024 * 1024)

#ifdef LINUX_BUILD
#include <stdlib.h>
static char *heap_memory = NULL;
#elif defined(TARGET_BPI)
static char *heap_memory = (char *)0x44000000;
#else
static char *heap_memory = (char *)0x00600000;
#endif

typedef struct BlockHeader {
    unsigned int size; // Size of the block including header
    int is_free;
    struct BlockHeader *next;
} BlockHeader;

static BlockHeader *free_list = NULL;

void mm_init() {
#ifdef LINUX_BUILD
    if (!heap_memory) heap_memory = (char*)malloc(HEAP_SIZE);
#endif

    free_list = (BlockHeader *)heap_memory;
    free_list->size = HEAP_SIZE;
    free_list->is_free = 1;
    free_list->next = NULL;
}

void *nykon_malloc(unsigned int size) {
    if (size == 0) return NULL;
    
    // Align size to 8 bytes
    size = (size + 7) & ~7;
    unsigned int total_size = size + sizeof(BlockHeader);
    
    BlockHeader *curr = free_list;
    BlockHeader *prev = NULL;
    
    while (curr) {
        if (curr->is_free && curr->size >= total_size) {
            // Found a block
            if (curr->size >= total_size + sizeof(BlockHeader) + 8) {
                // Split the block
                BlockHeader *new_block = (BlockHeader *)((char *)curr + total_size);
                new_block->size = curr->size - total_size;
                new_block->is_free = 1;
                new_block->next = curr->next;
                
                curr->size = total_size;
                curr->next = new_block;
            }
            
            curr->is_free = 0;
            return (void *)((char *)curr + sizeof(BlockHeader));
        }
        prev = curr;
        curr = curr->next;
    }
    
    return NULL; // Out of memory
}

void nykon_free(void *ptr) {
    if (!ptr) return;
    
    BlockHeader *header = (BlockHeader *)((char *)ptr - sizeof(BlockHeader));
    header->is_free = 1;
    
    // Simple coalesce: iterate through list and merge adjacent free blocks
    BlockHeader *curr = free_list;
    while (curr && curr->next) {
        if (curr->is_free && curr->next->is_free) {
            curr->size += curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

unsigned int mm_get_total_bytes(void) {
    return HEAP_SIZE;
}

unsigned int mm_get_used_bytes(void) {
    unsigned int used = 0;
    BlockHeader *curr = free_list;
    while (curr) {
        if (!curr->is_free) {
            used += curr->size;
        }
        curr = curr->next;
    }
    return used;
}

unsigned int mm_get_free_bytes(void) {
    unsigned int free_bytes = 0;
    BlockHeader *curr = free_list;
    while (curr) {
        if (curr->is_free) {
            free_bytes += curr->size;
        }
        curr = curr->next;
    }
    return free_bytes;
}
