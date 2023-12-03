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

void *write_to_disk(void *args)
{
  int block_id;
  int read_cache_idx;
  struct logfs *logfs = (struct logfs *)args;

  /* Lock The Mutex */
  pthread_mutex_lock(&logfs->mutex);

  /* Execute Until Forced To Exit */
  while (!logfs->exit_worker_thread)
  {
    while (logfs->write_buffer_filled < logfs->block_size)
    {
      /* Unlock Mutex And Exit Thread */
      if (logfs->exit_worker_thread)
      {
        pthread_mutex_unlock(&logfs->mutex);
        pthread_exit(NULL);
      }

      /* Wait For Signal */
      pthread_cond_wait(&logfs->item_avail, &logfs->mutex);
    }

    /* Get Block ID Of Block Being Currently Written To */
    block_id = logfs->file_offset / logfs->block_size;
    read_cache_idx = block_id % RCACHE_BLOCKS;

    if (logfs->read_cache[read_cache_idx]->block_id == block_id)
    {
      logfs->read_cache[read_cache_idx]->valid = 0;
    }

    /* Write To Disk */
    if (device_write(logfs->device, logfs->write_buffer + logfs->tail, logfs->file_offset, logfs->block_size))
    {
      TRACE("Error in device_write\n");
      pthread_exit(NULL);
    }

    /* Update Write Buffer Variables */
    logfs->tail = (logfs->tail + logfs->block_size) % logfs->write_buffer_size;
    logfs->write_buffer_filled = logfs->write_buffer_filled - logfs->block_size;
    logfs->file_offset = logfs->file_offset + logfs->block_size;
    if (logfs->head == logfs->tail || (logfs->tail == 0 && logfs->head == logfs->write_buffer_size))
    {
      pthread_cond_signal(&logfs->flush);
    }
  }
  pthread_exit(NULL);
}

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

int logfs_read(struct logfs *logfs, void *buf, uint64_t off, size_t len)
{
  int block_id;
  int read_buff_idx;
  int read_start_off;
  int len_to_read;
  size_t read_till_now = 0;

  /* Flush Buffer To Disk */
  flush_to_disk(logfs);

  block_id = off / logfs->block_size;
  read_buff_idx = block_id % RCACHE_BLOCKS;
  read_start_off = off % logfs->block_size;
  len_to_read = MIN(len, (size_t)logfs->block_size - read_start_off);

  while (read_till_now < len)
  {
    /* Read From Cache */
    if (logfs->read_cache[read_buff_idx] != NULL &&
        logfs->read_cache[read_buff_idx]->valid &&
        logfs->read_cache[read_buff_idx]->block_id == block_id)
    {
      memcpy((char *)buf + read_till_now, logfs->read_cache[read_buff_idx]->block + read_start_off, len_to_read);
    }
    /* Read From Disk */
    else
    {
      if ((device_read(logfs->device, logfs->read_cache[read_buff_idx]->block, block_id * logfs->block_size, logfs->block_size)))
      {
        TRACE("Error while calling device_read");
        return -1;
      }
      logfs->read_cache[read_buff_idx]->valid = 1;
      logfs->read_cache[read_buff_idx]->block_id = block_id;
      memcpy((char *)buf + read_till_now, logfs->read_cache[read_buff_idx]->block + read_start_off, len_to_read);
    }

    /* Update Block Parameters */
    read_till_now += len_to_read;
    block_id++;
    read_buff_idx = block_id % RCACHE_BLOCKS;
    read_start_off = 0;
    len_to_read = MIN((size_t)logfs->block_size, len - read_till_now);
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

int logfs_append(struct logfs *logfs, const void *buf, uint64_t len)
{
  uint64_t len_;

  len_ = len;
  pthread_mutex_lock(&logfs->mutex);
  while (len_ > 0)
  {
    while (logfs->write_buffer_size <= logfs->write_buffer_filled)
    {
      pthread_cond_wait(&logfs->space_avail, &logfs->mutex);
    }
    logfs->write_buffer_filled++;
    memcpy(logfs->write_buffer + logfs->head, (char *)buf + (len - len_), 1);
    len_--;
    logfs->head = (logfs->head + 1) % (logfs->write_buffer_size);
    pthread_cond_signal(&logfs->item_avail);
  }
  pthread_mutex_unlock(&logfs->mutex);
  return 0;
}