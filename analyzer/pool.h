/*

  Copyright (C) 2023 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, version 3.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this program.  If not, see
  <http://www.gnu.org/licenses/>

*/

#ifndef _SUSCAN_POOL_H
#define _SUSCAN_POOL_H

#include <sigutils/types.h>
#include <sigutils/defs.h>
#include <pthread.h>

#include "mq.h"

#define SUSCAN_POOL_MQ_TYPE_BUFFER  0
#define SUSCAN_POOL_MQ_TYPE_HALT   -1

struct suscan_sample_buffer_pool;

struct suscan_sample_buffer {
  struct suscan_sample_buffer_pool *parent;

  int        rindex; /* Reverse index in the buffer table */
  SUBOOL     circular;
  SUBOOL     acquired;

  SUCOMPLEX *data;
  SUSCOUNT   size;
};

typedef struct suscan_sample_buffer suscan_sample_buffer_t;

SUINLINE
SU_GETTER(suscan_sample_buffer, SUCOMPLEX *, data)
{
  return self->data;
}

SUINLINE
SU_GETTER(suscan_sample_buffer, SUSCOUNT, size)
{
  return self->size;
}

SU_INSTANCER(suscan_sample_buffer, struct suscan_sample_buffer_pool *);
SU_COLLECTOR(suscan_sample_buffer);

struct suscan_sample_buffer_pool_params {
  SUBOOL   vm_circularity;
  SUSCOUNT alloc_size;
  SUSCOUNT max_buffers;
};

#define suscan_sample_buffer_pool_params_INITIALIZER       \
{                                                          \
  SU_FALSE,                                                \
  512, /* alloc_size = 512 * 2 * sizeof(float32) = 4096 */ \
  16,                                                      \
}

/*
 * The buffer pool relies on a message queue to keep 
 * clients waiting for available buffers. This occurs when
 * buffer_count == max_buffers and all free buffers have been
 * allocated. In this situation, acquire hangs and try_acquire
 * returns NULL.
 * 
 */
struct suscan_sample_buffer_pool {
  struct suscan_sample_buffer_pool_params params;

  PTR_LIST(suscan_sample_buffer_t, buffer);
  struct suscan_mq free_mq;
  pthread_mutex_t  mutex;
  SUBOOL           mutex_init;
  SUBOOL           free_mq_init;
};

typedef struct suscan_sample_buffer_pool suscan_sample_buffer_pool_t;

SU_CONSTRUCTOR(
  suscan_sample_buffer_pool,
  const struct suscan_sample_buffer_pool_params *);
SU_DESTRUCTOR(suscan_sample_buffer_pool);

SU_INSTANCER(
  suscan_sample_buffer_pool,
  const struct suscan_sample_buffer_pool_params *);
SU_COLLECTOR(suscan_sample_buffer_pool);

SU_METHOD(suscan_sample_buffer_pool, suscan_sample_buffer_t *, acquire);
SU_METHOD(suscan_sample_buffer_pool, suscan_sample_buffer_t *, try_acquire);
SU_METHOD(suscan_sample_buffer_pool, SUBOOL, give, suscan_sample_buffer_t *);

#endif /* _SUSCAN_POOL */
