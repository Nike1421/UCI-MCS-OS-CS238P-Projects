/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * logfs.c
 */

#include <pthread.h>
#include "device.h"
#include "logfs.h"

#define WCACHE_BLOCKS 32
#define RCACHE_BLOCKS 256

/**
 * Needs:
 *   pthread_create()
 *   pthread_join()
 *   pthread_mutex_init()
 *   pthread_mutex_destroy()
 *   pthread_mutex_lock()
 *   pthread_mutex_unlock()
 *   pthread_cond_init()
 *   pthread_cond_destroy()
 *   pthread_cond_wait()
 *   pthread_cond_signal()
 */

/*
	Key Value Store instead of FIle Systems
	put(k,v)
	v=get(k)	Basic API's
	del(k)
	lsblk -> Identify all the disk and devices
	1) We need open, close, read, write, build logfs.c
	read(address, buffer, offset, length)
	read(address, buffer, offset, length)
		buffer is the location where I would like to read the data into
		offset - start reading data from offset, This should be block aligned, just like memory aligned, Always start from a multiple of block size
		length - The number of bytes we need to read, This neeeds to be in multiple of block size
		logfs always appends
		reads can be from anywhere to any length
	kvraw -> works on top of logfs, logfs just writes one buffer, kvraw, writes 2 buffers for key and value
		KVRAW Object - | metadata +| key +| value |  --> Meta contains size of key, size of value
		To update an existing key, create a new KVRAW Object and link it to the previous one [Reverse Linked List, FOr version history]
	Index -> when we get a key, we hash it and store the most recent location of it [HASHTABLE], stored in memory
	Create an iterative fn to write one block at a time if the required length is greater than a block
	Keep one page open, and keep appending to the end of it

	Create a Linked List, with 2 pointers, Head and Tail. Head is where we append the new arriving objects, tail is where the worker will take a block, write it to the disk and update the tail pointer to the next block
	Create a write buffer, that is more than 1 block, keep writing to this buffer and as soon as a block is written use threads to commit it to the disk
	Consumer Producer Problem in Concurrent Solutions
	Maintain a buffer size to show how much data we have in the queue,, and check if that is buff_size%block size > 1 == Write to disk
	Thread -> Wait until data becomes available, do not specify a time to wait for
	Use pthread condition variables
	Data at any point of time, can be in Cache, Write Buffer, and Disk
	To read-> Commit everything to disk before doing anything, so that we dont have to read anything from write buffer
	When writing a block to disk, fill the rest of the block with 0
	https://stackoverflow.com/questions/20772476/when-to-use-pthread-condition-variables

	For reading, Set aside a cache. If write buffer is 1mb, read buffer cna be 32mb
*/

/* research the above Needed API and design accordingly */

struct READ_CACHE_BLOCK
{
  int valid;
  int block_id;
  char *block;
  char *block_;
};

struct logfs
{
  /* Block Device */
  struct device *device;
  int file_offset;
  int block_size;

  /* Write Buffer */
  int head;
  int tail;
  char *write_buffer;
  char *write_buffer_;
  int write_buffer_size;
  int write_buffer_filled;

  /* Worker Thread */
  pthread_mutex_t mutex;
  pthread_cond_t space_avail;
  pthread_cond_t item_avail;
  pthread_cond_t flush;
  pthread_t worker_thread;
  int exit_worker_thread;

  /* Read Cache */
  struct READ_CACHE_BLOCK *read_cache[RCACHE_BLOCKS];
};
int flush_to_disk(struct logfs *logfs)
{
  int distance_to_move_head;

  pthread_mutex_lock(&logfs->mutex);
  distance_to_move_head = logfs->block_size - (logfs->head % logfs->block_size);
  logfs->head = logfs->head + distance_to_move_head;
  logfs->write_buffer_filled = logfs->write_buffer_filled + distance_to_move_head;

  pthread_cond_signal(&logfs->item_avail);
  pthread_cond_wait(&logfs->flush, &logfs->mutex);

  logfs->head = logfs->head - distance_to_move_head;
  logfs->tail = logfs->tail == 0 ? logfs->head - (logfs->head % logfs->block_size) : logfs->tail - logfs->block_size;
  logfs->write_buffer_filled = logfs->head % logfs->block_size;
  logfs->file_offset = logfs->file_offset - logfs->block_size;

  pthread_mutex_unlock(&logfs->mutex);
  return 0;
}

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
  int i;
    struct logfs *logfs;

  if (!(logfs = (struct logfs *)malloc(sizeof(struct logfs))))
    {
        TRACE("out of memory");
        return NULL;
    }
  if (!(logfs->device = device_open(pathname)))
  {
    TRACE("unable to open device");
    return NULL;
  }

  /* Set LogFS Block Size As Per Device Block Size */
  logfs->block_size = device_block(logfs->device);

  /* Initialize Write Buffer */
  logfs->write_buffer_size = WCACHE_BLOCKS * logfs->block_size;
  logfs->write_buffer_ = (char *)malloc(logfs->write_buffer_size + logfs->block_size);
  logfs->write_buffer = (char *)memory_align(logfs->write_buffer_, logfs->block_size);
  memset(logfs->write_buffer, 0, logfs->write_buffer_size);

  logfs->head = 0;
  logfs->tail = 0;
  logfs->write_buffer_filled = 0;
  logfs->exit_worker_thread = 0;
  logfs->file_offset = 0;

  /* Initialize Read Cache */
  for (i = 0; i < RCACHE_BLOCKS; ++i)
  {
    logfs->read_cache[i] = (struct READ_CACHE_BLOCK *)malloc(sizeof(struct READ_CACHE_BLOCK));
    logfs->read_cache[i]->block_ = (char *)malloc(2 * logfs->block_size);
    logfs->read_cache[i]->block = (char *)memory_align(logfs->read_cache[i]->block_, logfs->block_size);
    logfs->read_cache[i]->block_id = -1;
    logfs->read_cache[i]->valid = 0;
  }

  /* Initialize Mutexes and Condition Variables */
  if (pthread_mutex_init(&logfs->mutex, NULL) ||
      pthread_cond_init(&logfs->space_avail, NULL) ||
      pthread_cond_init(&logfs->item_avail, NULL) ||
      pthread_cond_init(&logfs->flush, NULL))
  {
    TRACE("Error in mutex and cond init");
    return NULL;
  }
  if (pthread_create(&logfs->worker_thread, NULL, &write_to_disk, logfs))
  {
    TRACE("Error in pthread_create");
        return NULL;
    }
    return logfs;
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
  int i;

  /* Flush All Current Contents Of Buffer To Disk */
  flush_to_disk(logfs);

  /* Exit Worker Thread */
  pthread_mutex_lock(&logfs->mutex);
  logfs->exit_worker_thread = 1;
  pthread_mutex_unlock(&logfs->mutex);
  pthread_cond_signal(&logfs->item_avail);
  pthread_join(logfs->worker_thread, NULL);

  /* Free All Read Cache Blocks */
  for (i = 0; i < RCACHE_BLOCKS; ++i)
  {
    FREE(logfs->read_cache[i]->block_);
    FREE(logfs->read_cache[i]);
  }

  /* Free Write Buffer */
  FREE(logfs->write_buffer_);

  /* Close Block Device */
  device_close(logfs->device);
    FREE(logfs);
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

int logfs_read(struct logfs *logfs, void *buf, uint64_t off, size_t len){
    if (device_read(logfs->block_device, buf, off, len))
    {
        TRACE(0);
        return -1;
    }
    return 0;
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

int logfs_append(struct logfs *logfs, const void *buf, uint64_t len);
