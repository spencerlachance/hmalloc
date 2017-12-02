#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "hmalloc.h"

/*
  typedef struct hm_stats {
  long pages_mapped;
  long pages_unmapped;
  long chunks_allocated;
  long chunks_freed;
  long free_length;
  } hm_stats;
*/

typedef struct node {
    size_t size;
    struct node* next;
} node;

const size_t PAGE_SIZE = 4096;
static hm_stats stats; // This initializes the stats to 0.
node* freelist;

void
print_free_list_help(int nn, node* free)
{
    printf("Free list node #%d, address = %ld, size = %ld, next = %ld, %ld\n",
            nn, (long)free, free->size, (long)(free->next), (long)free->next - (long)free);
    if (free->next != 0) {
        print_free_list_help(nn + 1, free->next);
    }
    else {
        printf("\n");
    }
}

void
print_free_list()
{
    if (freelist == 0) {
        printf("\n");
        return;
    }
    printf("Free list node #%d, address = %ld, size = %ld, next = %ld, %ld\n",
            1, (long)freelist, freelist->size, (long)(freelist->next), 
            (long)freelist->next - (long)freelist);
    if (freelist->next != 0) {
        print_free_list_help(2, freelist->next);
    }
    else {
        printf("\n");
    }
}

long
free_list_length_help(node* n)
{
    if (n->next == 0) {
        return 1;
    }
    return 1 + free_list_length_help(n->next);
}

long
free_list_length()
{
    if (freelist == 0) {
        return 0;
    }
    else {
        return free_list_length_help(freelist);
    }
}

node*
get_cell(size_t size) {
    for (node* n = freelist; n != 0; n = n->next) {
        if (n->size >= size) {
            return n;
        }
    }
    return 0;
}

void
coalesce()
{
    for (node* n = freelist; n != 0; n = n->next) {
        if (n->next == 0) {
            break;
        }
        while (((long)n) + n->size == ((long)n->next)) {
            n->size += n->next->size;
            n->next = n->next->next;
        }
    }
}

void
add_to_free_list(node* n)
{
    node* np = freelist;
    for (; ((long)np->next) < ((long)n); np = np->next) {
        if (np->next == 0) {
            break;
        }
    }

    if (np > n) {
        assert(np == freelist);
        n->next = freelist;
        freelist = n;
    }
    else {
        n->next = np->next;
        np->next = n;
    }
    //printf("Before coalesce\n");
    //print_free_list();
    coalesce();
}

hm_stats*
hgetstats()
{
    stats.free_length = free_list_length();
    return &stats;
}

void
hprintstats()
{
    stats.free_length = free_list_length();
    fprintf(stderr, "\n== husky malloc stats ==\n");
    fprintf(stderr, "Mapped:   %ld\n", stats.pages_mapped);
    fprintf(stderr, "Unmapped: %ld\n", stats.pages_unmapped);
    fprintf(stderr, "Allocs:   %ld\n", stats.chunks_allocated);
    fprintf(stderr, "Frees:    %ld\n", stats.chunks_freed);
    fprintf(stderr, "Freelen:  %ld\n", stats.free_length);
}

static
size_t
div_up(size_t xx, size_t yy)
{
    // This is useful to calculate # of pages
    // for large allocations.
    size_t zz = xx / yy;

    if (zz * yy == xx) {
        return zz;
    }
    else {
        return zz + 1;
    }
}

void*
hmalloc(size_t size)
{
    stats.chunks_allocated += 1;
    size += sizeof(size_t);

    void* result;
    if (size < PAGE_SIZE) {
        if (freelist == 0) {
            // Initialize free list
            stats.pages_mapped += 1;
            freelist = mmap(0, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
            freelist->size = PAGE_SIZE - sizeof(size_t);
            freelist->next = 0;
        }
        node* n = get_cell(size);
        if (n) {
            node* n_next = n->next;
            node* new_n = ((void*)n) + size;
            new_n->size = n->size - size;
            new_n->next = n_next;
            
            *((size_t*)n) = size;
            result = ((void*)n) + sizeof(size_t);
            
            if (n == freelist) {
                freelist = new_n;
            }
            else {
                node* np;
                for (np = freelist; ((long)np->next) != ((long)n); np = np->next) {}
                np->next = new_n;
            }
        }
        else {
            stats.pages_mapped += 1;
            void* h = mmap(0, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
            *((size_t*)h) = size;
            result = h + sizeof(size_t);
            
            node* new = (node*)((void*)h + size);
            new->size = PAGE_SIZE - size;
            new->next = 0;
        
            add_to_free_list(new);
        }
    }
    else {
        size_t num_pages = div_up(size, PAGE_SIZE);
        stats.pages_mapped += num_pages;
        size = num_pages * PAGE_SIZE;
        void* h = mmap(0, size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0); 
        *((size_t*)h) = size;
        result = h + sizeof(size_t);
    }
    return result;
}

void
hfree(void* item)
{
    stats.chunks_freed += 1;

    node* h = (node*) (item - sizeof(size_t));
    size_t size = *((size_t*)h);

    if (size < PAGE_SIZE) {
        h->size = size;
        h->next = 0;
        add_to_free_list(h);
    }
    else {
        size_t num_pages = div_up(size, PAGE_SIZE);
        stats.pages_unmapped += num_pages;
        munmap(h, size);
    }
}
