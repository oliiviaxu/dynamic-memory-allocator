#include <stdint.h>
#include <string.h>

#include "memlib.h"
#include "mm.h"

/** The required alignment of heap payloads */
const size_t ALIGNMENT = 2 * sizeof(size_t);

/** The layout of each block allocated on the heap */
typedef struct {
    /** The size of the block and whether it is allocated (stored in the low bit) */
    size_t header;
    /**
     * We don't know what the size of the payload will be, so we will
     * declare it as a zero-length array.  This allow us to obtain a
     * pointer to the start of the payload.
     */
    uint8_t payload[];
} block_t;

// free blocks 
typedef struct {
    size_t header;
    block_t *prev;
    block_t *next;
} free_block_t;

typedef struct {
    size_t size;
} footer_t;

static free_block_t *head = NULL;
/** The first and last blocks on the heap */
static block_t *mm_heap_first = NULL;
static block_t *mm_heap_last = NULL;



void add_block(block_t *block) {
    free_block_t *free_block = (free_block_t *)block;
    if (head == NULL) {
        head = free_block;
        head->prev = NULL;
        head->next = NULL;
        return;
    } 
    free_block_t *old = head;
    head = free_block;
    head->prev = NULL;
    head->next = (block_t *)old;
    old->prev = (block_t *) head;
}

void remove_block(block_t *block) {
    free_block_t *free_block = (free_block_t *)block;
    free_block_t *prev = (free_block_t *)free_block->prev;
    free_block_t *nxt = (free_block_t *)free_block->next;
    if (prev != NULL) {
        prev->next = (block_t *)nxt;
    } else {
        head = nxt;
    }
    if (nxt != NULL) {
        nxt->prev = (block_t *)prev;
    }
}

/** Rounds up `size` to the nearest multiple of `n` */
static size_t round_up(size_t size, size_t n) {
    return (size + (n - 1)) / n * n;
}

/** Set's a block's header with the given size and allocation state */
static void set_header(block_t *block, size_t size, bool is_allocated) {
    block->header = size | is_allocated;
    footer_t *footer = (void *) block + size - sizeof(footer_t);
    footer->size = size;
}

/** Extracts a block's size from its header */
static size_t get_size(block_t *block) {
    return block->header & ~1;
}

/** Extracts a block's allocation state from its header */
static bool is_allocated(block_t *block) {
    return block->header & 1;
}

/**
 * Finds the first free block in the heap with at least the given size.
 * If no block is large enough, returns NULL.
 */
static block_t *find_fit(size_t size) {
    // Traverse the blocks in the heap using the implicit list
    for (free_block_t *curr = head; curr != NULL; curr = (free_block_t *) curr->next) {
        if (get_size((block_t *) curr) >= size) {
            return (block_t *) curr;
        }
    }
    return NULL;
}

/** Gets the header corresponding to a given payload pointer */
static block_t *block_from_payload(void *ptr) {
    return ptr - offsetof(block_t, payload);
}

/**
 * mm_init - Initializes the allocator state
 */
bool mm_init(void) {
    // We want the first payload to start at ALIGNMENT bytes from the start of the heap
    void *padding = mem_sbrk(ALIGNMENT - sizeof(block_t));
    if (padding == (void *) -1) {
        return false;
    }
    head = NULL;
    // Initialize the heap with no blocks
    mm_heap_first = NULL;
    mm_heap_last = NULL;
    return true;
}

void split_helper(block_t *block, block_t *split, size_t size, size_t split_size) {
    if (block == mm_heap_last) {
        mm_heap_last = split;
    }
    set_header(block, size, true);
    set_header(split, split_size, false);
    add_block(split);
}
/**
 * mm_malloc - Allocates a block with the given size
 */
void *mm_malloc(size_t size) {
    // The block must have enough space for a header and be 16-byte aligned
    size = round_up(sizeof(block_t) + size + sizeof(footer_t), ALIGNMENT);

    block_t *block = find_fit(size);
    if (block != NULL) {
        remove_block(block);
        if (get_size(block) > sizeof(block_t) + size + sizeof(footer_t)) { 
            block_t *split = (void *) block + size;
            size_t split_size = get_size(block) - size;
            split_helper(block, split, size, split_size);
        } else {
            set_header(block, get_size(block), true);
        }
        return block->payload;
    }

    // Otherwise, a new block needs to be allocated at the end of the heap
    block = mem_sbrk(size);
    if (block == (void *) -1) {
        return NULL;
    }

    // Update mm_heap_first and mm_heap_last since we extended the heap
    if (mm_heap_first == NULL) {
        mm_heap_first = block;
    }
    mm_heap_last = block;

    // Initialize the block with the allocated size
    set_header(block, size, true);
    return block->payload;
}

void coalesce_helper(block_t *prev, block_t *block, block_t *next) {
    if (prev != NULL && next != NULL && !is_allocated(prev) && !is_allocated(next)) {
        set_header(prev, get_size(prev) + get_size(block) + get_size(next), false);
        remove_block(next);
        remove_block(block);
        if (next == mm_heap_last) {
            mm_heap_last = prev;
        }
    } else if (prev != NULL && !is_allocated(prev)) {
        set_header(prev, get_size(prev) + get_size(block), false);
        remove_block(block);
        if (block == mm_heap_last) {
            mm_heap_last = prev;
        }
    } else if (next != NULL && !is_allocated(next)) {
        set_header(block, get_size(block) + get_size(next), false);
        remove_block(next);
        if (next == mm_heap_last) {
            mm_heap_last = block;
        }
    } else {
        set_header(block, get_size(block), false);
    }
}

/**
 * mm_free - Releases a block to be reused for future allocations
 */
void mm_free(void *ptr) {
    // mm_free(NULL) does nothing
    if (ptr == NULL) {
        return;
    }

    block_t *block = block_from_payload(ptr);  
    add_block(block);

    block_t *prev = NULL;
    block_t *next = NULL;
    if (block != mm_heap_first) {
        footer_t *footer = (void *) block - sizeof(footer);
        prev = (void *) block - footer->size;
    }
    if (block != mm_heap_last) {
        next = (void *) block + get_size(block);
    }

    //coalesce 
    coalesce_helper(prev, block, next);
}
    
/**
 * mm_realloc - Change the size of the block by mm_mallocing a new block,
 *      copying its data, and mm_freeing the old block.
 */
void *mm_realloc(void *old_ptr, size_t size) {
    void *new_ptr = mm_malloc(size);
    if (old_ptr == NULL) {
        return new_ptr;
    }
    if (size == 0) {
        mm_free(old_ptr);
        return NULL;
    }
    block_t *old_block =  block_from_payload(old_ptr);
    size_t new_size = get_size(old_block);
    if (new_size > size) {
        memcpy(new_ptr, old_ptr, size);
    } else {
        memcpy(new_ptr, old_ptr, new_size);
    }
    mm_free(old_ptr);
    return new_ptr;
}

/**
 * mm_calloc - Allocate the block and set it to zero.
 */
void *mm_calloc(size_t nmemb, size_t size) {
    size_t total_size = nmemb * size;
    void *block = mm_malloc(total_size);
    memset(block, 0, nmemb * size);
    return block;
}

/**
 * mm_checkheap - So simple, it doesn't need a checker!
 */
void mm_checkheap(void) {
}