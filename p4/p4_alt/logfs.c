#include "logfs.h"
#include "device.h"
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#define WCACHE_BLOCKS 32
#define RCACHE_BLOCKS 256
#define BLOCK_SIZE 4096

struct logfs_block
{
  uint64_t id;
  char *data;
  uint64_t size;
  struct logfs_block *next;
};

struct logfs_cache
{
  struct logfs_block *head;
  struct logfs_block *tail;
};

struct logfs
{
  uint64_t size;
  struct device *device;
  struct logfs_cache wcache;
  struct logfs_cache rcache;
  uint64_t wcache_blocks;
  uint64_t rcache_blocks;
  pthread_t worker_thread;
};

static pthread_mutex_t wcache_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wcache_cond = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t rcache_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rcache_cond = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t device_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *worker_thread_function(void *arg);

/**
 * Opens the block device specified in pathname for buffered I/O using an
 * append only log structure.
 *
 * pathname: the pathname of the block device
 *
 * return: an opaque handle or NULL on error
 */
struct logfs *logfs_open(const char *pathname)
{
  struct logfs *logfs;
  if (!(logfs = malloc(sizeof(struct logfs))))
  {
    perror("Error allocating memory for logfs");
    return NULL;
  }
  memset(logfs, 0, sizeof(struct logfs));

  if (!(logfs->device = device_open(pathname)))
  {
    perror("Error opening block device");
    free(logfs);
    return NULL;
  }

  /* Circular Linked List For Write And Read Cache */
  logfs->wcache.head = logfs->wcache.tail = NULL;
  logfs->rcache.head = logfs->rcache.tail = NULL;

  /* Create Worker Thread */
  pthread_mutex_lock(&wcache_mutex);
  pthread_mutex_lock(&rcache_mutex);
  if (pthread_create(&logfs->worker_thread, NULL, worker_thread_function,
                     (void *)logfs) != 0)
  {
    perror("Error creating worker thread");
    device_close(logfs->device);
    free(logfs);
    return NULL;
  }

  return logfs;
}

void destroy_cache(struct logfs_cache *cache)
{
  struct logfs_block *current = cache->head;
  while (current != NULL)
  {
    struct logfs_block *next = current->next;
    free(current->data);
    free(current);
    current = next;
  }
  cache->head = cache->tail = NULL;
}

struct logfs_block *create_new_block(pthread_mutex_t *mutex)
{
  struct logfs_block *new_block = malloc(sizeof(struct logfs_block));
  if (!new_block)
  {
    perror("Error allocating memory for logfs block");
    pthread_mutex_unlock(mutex);
    return NULL;
  }
  memset(new_block, 0, sizeof(struct logfs_block));

  new_block->next = NULL;
  new_block->data = malloc(BLOCK_SIZE);
  if (!new_block->data)
  {
    free(new_block);
    pthread_mutex_unlock(mutex);
    perror("Error allocating memory for logfs block data");
    return NULL;
  }
  memset(new_block->data, 0, BLOCK_SIZE);
  return new_block;
}

/**
 * Closes a previously opened logfs handle.
 *
 * logfs: an opaque handle previously obtained by calling logfs_open()
 *
 * Note: logfs may be NULL.
 */
void logfs_close(struct logfs *logfs)
{
  if (!logfs)
  {
    return;
  }

  /* Free Write And Read Caches */
  destroy_cache(&logfs->wcache);
  destroy_cache(&logfs->rcache);

  /* Cancel Worker Thread */
  pthread_cancel(logfs->worker_thread);

  /* Wait for the worker thread to finish */
  pthread_join(logfs->worker_thread, NULL);
  pthread_mutex_unlock(&wcache_mutex);
  pthread_mutex_unlock(&rcache_mutex);

  /* Close Device */
  device_close(logfs->device);
  free(logfs);
}

/**
 * Append len bytes to the logfs.
 *
 * logfs: an opaque handle previously obtained by calling logfs_open()
 * buf  : a region of memory holding the len bytes to be written
 * len  : the number of bytes to write
 *
 * return: 0 on success, otherwise error
 */
int logfs_append(struct logfs *logfs, const void *buf, uint64_t len)
{
  if (!logfs)
  {
    return -1;
  }

  /* Lock Write Cache */
  pthread_mutex_lock(&wcache_mutex);

  /* Append data to the write cache */
  while (len > 0)
  {
    /* Calculate the remaining space in the current block */
    uint64_t remaining_space = BLOCK_SIZE - (logfs->size % BLOCK_SIZE);

    /* Determine the size to copy in this iteration */
    uint64_t copy_size = len < remaining_space ? len : remaining_space;

    /* Allocate a new block if needed */
    if (remaining_space == BLOCK_SIZE)
    {
      /* Link the new block to the write cache */
      struct logfs_block *new_block = create_new_block(&wcache_mutex);
      new_block->id = logfs->wcache_blocks++;
      if (logfs->wcache.head == NULL)
      {
        logfs->wcache.head = logfs->wcache.tail = new_block;
      }
      else
      {
        logfs->wcache.tail->next = new_block;
        logfs->wcache.tail = new_block;
      }
    }

    /* Copy data to the current block */
    memcpy(logfs->wcache.tail->data + (logfs->wcache.tail->size % BLOCK_SIZE),
           buf, copy_size);

    /* Update variables for the next iteration */
    len -= copy_size;
    logfs->wcache.tail->size += copy_size;
    logfs->size += copy_size;
  }

  /* Signal the worker thread that there's new data in the write cache */
  pthread_cond_signal(&wcache_cond);
  pthread_mutex_unlock(&wcache_mutex);
  return 0;
}

int read_from_device(struct logfs *logfs, uint64_t off, struct logfs_block *new_block)
{
  pthread_mutex_lock(&device_mutex);
  if (device_read(logfs->device, new_block->data,
                  device_block(logfs->device) *
                      (off / device_block(logfs->device)),
                  device_block(logfs->device)))
  {
    TRACE("Error reading from device");
    pthread_mutex_unlock(&device_mutex);
    return 1;
  }
  pthread_mutex_unlock(&device_mutex);
  return 0;
}


/**
 * Random read of len bytes at location specified in off from the logfs.
 *
 * logfs: an opaque handle previously obtained by calling logfs_open()
 * buf  : a region of memory large enough to receive len bytes
 * off  : the starting byte offset
 * len  : the number of bytes to read
 *
 * return: 0 on success, otherwise error
 */
int logfs_read(struct logfs *logfs, void *buf, uint64_t off, uint64_t len)
{
  struct logfs_block *block;
  struct logfs_block *new_block;
  if (!logfs)
  {
    return -1;
  }

  us_sleep(10);
  /* printf("LOCKING READ CACHE\n"); */
  pthread_mutex_lock(&rcache_mutex);

  block = logfs->rcache.head;

  if (off + len > logfs->size)
  {
    pthread_cond_signal(&wcache_cond);
    pthread_cond_wait(&rcache_cond, &rcache_mutex);
  }

  while (block != NULL)
  {
    if (block->id == off / device_block(logfs->device))
    {
      /* Block found in read cache */
      if (read_from_device(logfs, off, block))
      {
        pthread_mutex_unlock(&rcache_mutex);
        return 0;
      }
      memcpy(buf, block->data + off % device_block(logfs->device), len);
      pthread_mutex_unlock(&rcache_mutex);
      return 0;
    }
    block = block->next;
  }

  /* Block not found in read cache, perform a regular read */
  new_block = create_new_block(&rcache_mutex);
  new_block->id = off / device_block(logfs->device);
  new_block->next = logfs->rcache.head;
  
  if (read_from_device(logfs, off, new_block))
  {
    pthread_mutex_unlock(&rcache_mutex);
  }
  
  new_block->size = device_block(logfs->device);
  memcpy(buf, new_block->data + off % device_block(logfs->device), len);
  
  logfs->rcache.head = new_block;

  pthread_mutex_unlock(&rcache_mutex);
  return 0;
}

int write_to_device(struct logfs *logfs, struct logfs_block *current)
{
  pthread_mutex_lock(&device_mutex);
  if (device_write(logfs->device, current->data, current->id * BLOCK_SIZE,
                   BLOCK_SIZE))
  {
    TRACE("Error writing to device");
    pthread_mutex_unlock(&device_mutex);
    return 1;
  }
  pthread_mutex_unlock(&device_mutex);
  return 0;
}

static void *worker_thread_function(void *arg)
{
  struct logfs_block *current, *next;
  struct logfs *logfs = (struct logfs *)arg;
  
  pthread_mutex_unlock(&rcache_mutex);
  pthread_mutex_unlock(&wcache_mutex);
  
  while (1)
  {
    pthread_mutex_lock(&wcache_mutex);
    pthread_cond_wait(&wcache_cond, &wcache_mutex);
    pthread_mutex_lock(&rcache_mutex);
    /* Wait for a signal indicating new data in the write cache */

    /* Perform device_write for each block in the write cache */
    current = logfs->wcache.head;
    logfs->size = 0;
    while (current != NULL)
    {
      if (write_to_device(logfs, current))
      {
        TRACE("Error writing to device");
        return NULL;
      }
      
      logfs->size += current->size;
      next = current->next;
      current = next;
    }

    /* Reset the write cache */
    pthread_cond_signal(&rcache_cond);
    pthread_mutex_unlock(&rcache_mutex);
    pthread_mutex_unlock(&wcache_mutex);
  }
}
