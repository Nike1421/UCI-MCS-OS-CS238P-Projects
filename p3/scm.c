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
#define MULT 1

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

    /* Map The Input File To Virtual Memory */
    if ((scm->addr = mmap((void *) VIRT_ADDR, scm->size.capacity, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED,
                          scm->fd, 0)) == MAP_FAILED) {
        close(scm->fd);
        free(scm);
        TRACE("mmap Error");
        return NULL;
    }

    /* Truncate Flag Is Passed */
    if (truncate) {
        if (ftruncate(scm->fd, scm->size.capacity) == -1) {
            close(scm->fd);
            free(scm);
            TRACE("Truncate Error");
            return NULL;
        }
        scm->size.utilized = 0;
    } else {
        /* Get How Much Space Has Been Utilized By Reading The First size_t Bytes In The Mapped Region Of The File*/
        scm->size.utilized = (size_t) *(size_t *) scm->addr;
        printf("SCM Utilization: %lu\n", scm->size.utilized);
    }
    /* Start storing data from address after struct */
    scm->addr = (char *) scm->addr + sizeof(struct scm);
    printf("SCM Now Located @: %p\n", scm->addr);

    return scm;
}

/**
 * Closes a previously opened SCM handle.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 *
 * Note: scm may be NULL
 */

void scm_close(struct scm *scm) {
    if (scm) {
        printf("SCM Located @: %p\n", scm->addr);
        printf("SCM Utilization: %lu\n", scm->size.utilized);

        /* Sync Any Modifications Present In Cache To The File */
        msync((char *) VIRT_ADDR, scm->size.capacity, MS_SYNC);

        /* Unmap The File From The Virtual Memory */
        munmap((char *) VIRT_ADDR, scm->size.capacity);

        close(scm->fd);
        memset(scm, 0, sizeof(struct scm));
}
    free(scm);
}

/**
 * Analogous to the standard C malloc function, but using SCM region.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 * n  : the size of the requested memory in bytes
 *
 * return: a pointer to the start of the allocated memory or NULL on error
 */

    /* BASE Address From Where The Data Is Being Stored */
    void *p = (char *) scm->addr + scm->size.utilized;
    
    /* Memory Block Size */
    size_t size = sizeof(short) + sizeof(size_t) + n;
    
    /* Condition To Check If All Memory Allocated Has Been Utilized */
    /* Ideally, We Should Allocate More Memory Here */
    if (scm->size.utilized + size > scm->size.capacity) {
        /* TODO: In The Very Far Future */
            return NULL;
    }

    /* set the bit value to 1 */
    *(short *) p = 1;
    /* set size to size */
    *(size_t *) ((char *) p + sizeof(short)) = size;
    /* Increase Utilized Size */
    scm->size.utilized += size;
    /* Save How Much Space Has Been Utilized To Teh First size_t Bytes In The Mapped File  */
    *(size_t *) ((char *) scm->addr - sizeof(size_t)) = scm->size.utilized;
    /* Memory BASE ADDR */
    return (char *) p + sizeof(short) + sizeof(size_t);
}

/**
 * Analogous to the standard C strdup function, but using SCM region.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 * s  : a C string to be duplicated.
 *
 * return: the base memory address of the duplicated C string or NULL on error
 */

char *scm_strdup(struct scm *scm, const char *s) {
    size_t n = strlen(s) + 1;
    char *p = scm_malloc(scm, n);
    if (!p) {
        return NULL;
    }
    memcpy(p, s, n);
    return p;
}

/**
 * Analogous to the standard C free function, but using SCM region.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 * p  : a pointer to the start of a previously allocated memory
 */

void scm_free(struct scm *scm, void *p) {
    size_t size = *(size_t *) ((char *) p - sizeof(size_t));
    *(short *) ((char *) p - sizeof(short) - sizeof(size_t)) = 0;
    scm->size.utilized -= size;
    *(size_t *) ((char *) scm->addr - sizeof(size_t)) = scm->size.utilized;
}

/**
 * Returns the number of SCM bytes utilized thus far.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 *
 * return: the number of bytes utilized thus far
 */

size_t scm_utilized(const struct scm *scm) {
    return scm->size.utilized;
}

/**
 * Returns the number of SCM bytes available in total.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 *
 * return: the number of bytes available in total
 */

size_t scm_capacity(const struct scm *scm) {
    return scm->size.capacity;
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

void *scm_mbase(struct scm *scm) {
    /* the base address of the scm is VIRT_ADDR */
    /* the base address of the scm is different from the original address because of the T and CRC */
    /* the base address of the scm is the address of the metadata */
    return (char *) scm->addr + sizeof(short) + sizeof(size_t);
}