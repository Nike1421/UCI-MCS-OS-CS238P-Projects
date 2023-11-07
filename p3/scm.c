/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * scm.c
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include "scm.h"

#define VIRT_ADDR 0x600000000000

/**
 * Uses:
 *   fstat()
 *   S_ISREG()
 *   open()
 *   close()
 *   sbrk()
 *   mmap()
 *   munmap()
 *   msync()
 */

struct scm {
    /* File Descriptor */
    int fd; 
    struct {
        size_t utilized; /* Utilized Bytes */
        size_t capacity; /* Available Bytes */
    } size;
    /* Memory Address of SCM in the Heap */
    void *addr; 
};

/**
 * 
*/
struct scm *file_size(const char *pathname) {
    struct stat st;
    int fd;
    struct scm *scm;

    assert(pathname);

    /* Allocate Some Memory To The SCM */
    if (!(scm = malloc(sizeof(struct scm)))) {
        return NULL;
    }
    memset(scm, 0, sizeof(struct scm));

    /* Open The File */
    if ((fd = open(pathname, O_RDWR)) == -1) {
        free(scm);
        TRACE("File Descriptor ERROR");
        return NULL;
    }

    if (fstat(fd, &st) == -1) {
        free(scm);
        close(fd);
        TRACE("File Descriptor fstat ERROR");
        return NULL;
    }

    if (!S_ISREG(st.st_mode)) {
        free(scm);
        close(fd);
        TRACE("File Is Not Regular");
        return NULL;
    }

    scm->fd = fd;
    scm->size.utilized = 0;
    scm->size.capacity = st.st_size;

    return scm;
}

/**
 * Initializes an SCM region using the file specified in pathname as the
 * backing device, opening the regsion for memory allocation activities.
 *
 * pathname: the file pathname of the backing device
 * truncate: if non-zero, truncates the SCM region, clearning all data
 *
 * return: an opaque handle or NULL on error
 */

struct scm *scm_open(const char *pathname, int truncate) {
    /* Initialize SCM */
    struct scm *scm = file_size(pathname);
    if (!scm) {
        TRACE("SCM");
        return NULL;
    }

    /* Increment Memory Break */
    if (sbrk(scm->size.capacity) == (void *) -1) {
        close(scm->fd);
        free(scm);
        TRACE("sbrk error");
        return NULL;
    }

    /* Map the input file to Virtual Memory */
    if ((scm->addr = mmap((void *) VIRT_ADDR, scm->size.capacity, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED,
                          scm->fd, 0)) == MAP_FAILED) {
        close(scm->fd);
        free(scm);
        TRACE("mmap error");
        return NULL;
    }

    /* Truncate Flag Is Passed */
    if (truncate) {
        if (ftruncate(scm->fd, scm->size.capacity) == -1) {
            close(scm->fd);
            free(scm);
            TRACE("truncate error");
            return NULL;
        }
        scm->size.utilized = 0;
    } else {
        /* get the value of the utilized memory from the first sizeof(size_t) bytes (size_t) *(size_t *) scm->addr;*/
        scm->size.utilized = (size_t) *(size_t *) scm->addr;
        printf("scm->size.utilized: %lu\n", scm->size.utilized);
    }
    /* Start storing data from address after struct */
    scm->addr = (char *) scm->addr + sizeof(struct scm);
    printf("scm->addr after: %p\n", scm->addr);

    return scm;
}

/**
 * Closes a previously opened SCM handle.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 *
 * Note: scm may be NULL
 */

void scm_close(struct scm *scm)
{
    if (scm)
    {
        close(scm->fd);
        free(scm);
    }
}

/* findChunk: search for the first chunk that fits (size equal or more) the request
              of the user.
     chunkStatus *headptr: pointer to the first block of memory in the heap
     unsigned int size: size requested by the user
     retval: a poiter to the block which fits the request
         or NULL, in case there is no such block in the list
*/
struct data_block *findBlock(struct scm* scm, size_t size)
{
    struct data_block *ptr = scm->head;

    while (ptr != NULL)
    {
        if (ptr->block_size >= (size + sizeof(struct data_block)) && ptr->is_free == 1)
        {
            return ptr;
        }
        scm->last_visited = ptr;
        ptr = ptr->next;
    }
    return ptr;
}

/* splitChunk: split one big block into two. The first will have the size requested by the user.
           the second will have the remainder.
     chunkStatus* ptr: pointer to the block of memory which is going to be splitted.
     unsigned int size: size requested by the user
     retval: void, the function modifies the list
*/
void splitChunk(struct data_block *ptr, size_t size)
{
    struct data_block* newChunk = ptr;

    newChunk->addr = ptr->addr + size;
    newChunk->block_size = ptr->block_size - size - sizeof(struct data_block);
    newChunk->is_free = 1;
    newChunk->next = ptr->next;
    newChunk->prev = ptr;

    if ((newChunk->next) != NULL)
    {
        (newChunk->next)->prev = newChunk;
    }

    ptr->block_size = size;
    ptr->is_free = 0;
    ptr->next = newChunk;
}

/**
 * Analogous to the standard C malloc function, but using SCM region.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 * n  : the size of the requested memory in bytes
 *
 * return: a pointer to the start of the allocated memory or NULL on error
 */

void *scm_malloc(struct scm *scm, size_t n)
{
    void * currPositionOfBreak;
    size_t memory_need = MULTIPLIER * (n + sizeof(struct data_block));
    /* void* break_pointer; */
    /* Blocks x2 */
    /* struct data_block *head = scm->head; */
    /* struct data_block *ptr; */

    /* break_pointer = sbrk(0); */
    printf("%ld\n", scm->addr + memory_need - (char *)sbrk(0));

    if (scm->head == NULL)
    {
        currPositionOfBreak = sbrk(0);
        printf("SBREAK LIMIT p : %p\n", scm->addr);
        /* if (sbrk(scm->addr + memory_need - (char *)sbrk(0)) == (void *)-1)
        {
            perror("FFF");
            return NULL;
        } */

        currPositionOfBreak = sbrk(0);
        /* printf("SBREAK LIMIT p : %p\n", currPositionOfBreak); */
        printf("BReak Pointer LIMIT p : %p\n", currPositionOfBreak);
        /* break_pointer = scm->addr; */
        printf("HI\n");

        scm->head = currPositionOfBreak;
        scm->head->block_size = memory_need - sizeof(struct data_block);
        scm->head->is_free = 0;
        scm->head->next = NULL;
        scm->head->prev = NULL;

        printf("%p\n", scm->addr);

        if (MULTIPLIER > 1)
        {
            /* splitChunk(ptr, size); */
        }
        return scm->head;
    }
    else
    {
        struct data_block *free_block = NULL;
        free_block = findBlock(scm, n);
        if (!free_block)
        {
            /* free_block = increaseAllocation(scm->last_visited, n);
            if (!free_block)
            {
                return NULL;
            }
            return free_block->addr; */
        }

        else 
        {
            if (free_block->block_size > n) 
            {
                splitChunk(free_block, n);
            }
        }
        /* pthread_mutex_unlock(&lock); */
        return free_block->addr;
    }
}

/**
 * Analogous to the standard C strdup function, but using SCM region.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 * s  : a C string to be duplicated.
 *
 * return: the base memory address of the duplicated C string or NULL on error
 */

char *scm_strdup(struct scm *scm, const char *s)
{
    size_t n = strlen(s) + 1;
    char *addr = scm_malloc(scm, n);
    if (addr == NULL)
    {
        return NULL;
    }
    memcpy(addr, s, n);
    return addr;
}

/**
 * Analogous to the standard C free function, but using SCM region.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 * p  : a pointer to the start of a previously allocated memory
 */

/* void scm_free(struct scm *scm, void *p); */

/**
 * Returns the number of SCM bytes utilized thus far.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 *
 * return: the number of bytes utilized thus far
 */

size_t scm_utilized(const struct scm *scm)
{
    return scm->size_utilized;
}

/**
 * Returns the number of SCM bytes available in total.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 *
 * return: the number of bytes available in total
 */

size_t scm_capacity(const struct scm *scm)
{
    return scm->size_available;
}

/**
 * Returns the base memory address withn the SCM region, i.e., the memory
 * pointer that would have been returned by the first call to scm_malloc()
 * after a truncated initialization.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 *
 * return: the base memory address within the SCM region
 */

void *scm_mbase(struct scm *scm)
{
    return scm->addr;
}
