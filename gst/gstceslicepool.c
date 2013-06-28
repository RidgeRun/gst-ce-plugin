/*
 * gstceslicepool.c
 *
 * Copyright (C) 2013 RidgeRun, LLC (http://www.ridgerun.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses>.
 *
 */

#include "gstcmemallocator.h"
#include "gstceslicepool.h"

#define GST_SLICE_POOL_LOCK(pool)   (g_rec_mutex_lock(&pool->priv->rec_lock))
#define GST_SLICE_POOL_UNLOCK(pool) (g_rec_mutex_unlock(&pool->priv->rec_lock))

/* memory slice*/
typedef struct _memSlice
{
  gint start;
  gint end;
  gint size;
} memSlice;

/* bufferpool */
struct _GstCeSliceBufferPoolPrivate
{
  GRecMutex rec_lock;

  GList *slices;
  memSlice *last_slice;

  gint cur_buffers;
  gint max_buffers;
  gint buffer_size;
  gint memory_block_size;

  GstMemory *memory;
  guint8 *data;

  GstAllocator *allocator;
  GstAllocationParams params;
};

static void gst_ce_slice_buffer_pool_finalize (GObject * object);
static gboolean ce_slice_buffer_pool_set_config (GstBufferPool * pool,
    GstStructure * config);
static gboolean ce_slice_buffer_pool_start (GstBufferPool * pool);
static gboolean ce_slice_buffer_pool_stop (GstBufferPool * pool);
static GstFlowReturn ce_slice_buffer_pool_buffer_alloc (GstBufferPool * pool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params);
static void ce_slice_buffer_pool_release_buffer (GstBufferPool * pool,
    GstBuffer * buffer);
static GstFlowReturn ce_slice_buffer_pool_acquire_buffer (GstBufferPool * pool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params);

GList *get_slice (GstCeSliceBufferPool * spool, gint * size);

#define GST_CE_SLICE_BUFFER_POOL_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_CE_SLICE_BUFFER_POOL, GstCeSliceBufferPoolPrivate))

#define gst_ce_slice_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstCeSliceBufferPool, gst_ce_slice_buffer_pool,
    GST_TYPE_BUFFER_POOL);

static void
gst_ce_slice_buffer_pool_class_init (GstCeSliceBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  g_type_class_add_private (klass, sizeof (GstCeSliceBufferPoolPrivate));

  gobject_class->finalize = gst_ce_slice_buffer_pool_finalize;
  gstbufferpool_class->set_config = ce_slice_buffer_pool_set_config;
  gstbufferpool_class->start = ce_slice_buffer_pool_start;
  gstbufferpool_class->stop = ce_slice_buffer_pool_stop;
  gstbufferpool_class->alloc_buffer = ce_slice_buffer_pool_buffer_alloc;
  gstbufferpool_class->release_buffer = ce_slice_buffer_pool_release_buffer;
  gstbufferpool_class->acquire_buffer = ce_slice_buffer_pool_acquire_buffer;
}

static void
gst_ce_slice_buffer_pool_init (GstCeSliceBufferPool * pool)
{
  GstCeSliceBufferPoolPrivate *priv;
  pool->priv = priv = GST_CE_SLICE_BUFFER_POOL_GET_PRIVATE (pool);

  g_rec_mutex_init (&priv->rec_lock);

  priv->allocator = NULL;
  gst_allocation_params_init (&priv->params);

}

static void
gst_ce_slice_buffer_pool_finalize (GObject * object)
{
  GstCeSliceBufferPool *pool = GST_CE_SLICE_BUFFER_POOL_CAST (object);
  GstCeSliceBufferPoolPrivate *priv = pool->priv;

  GST_LOG_OBJECT (pool, "finalize video buffer pool %p", pool);

  g_rec_mutex_clear (&priv->rec_lock);
  if (priv->allocator)
    gst_object_unref (priv->allocator);

  G_OBJECT_CLASS (gst_ce_slice_buffer_pool_parent_class)->finalize (object);
}

static gboolean
ce_slice_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstCeSliceBufferPool *spool = GST_CE_SLICE_BUFFER_POOL_CAST (pool);
  GstCeSliceBufferPoolPrivate *priv = spool->priv;
  GstCaps *caps;
  guint size, min_buffers, max_buffers;
  GstAllocator *allocator;
  GstAllocationParams params;

  /* parse the config and keep around */
  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
          &max_buffers))
    goto wrong_config;

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &params))
    goto wrong_config;

  if (!GST_IS_CMEM_ALLOCATOR (allocator))
    goto no_cmem_allocator;

  GST_DEBUG_OBJECT (pool, "config %" GST_PTR_FORMAT, config);

  priv->buffer_size = size;
  priv->max_buffers = max_buffers;
  priv->memory_block_size = max_buffers * size;

  GST_DEBUG_OBJECT (pool,
      "config buffer size %d, max buffers %d, memory block size %d", size,
      max_buffers, priv->memory_block_size);
  if (priv->allocator)
    gst_object_unref (priv->allocator);
  if ((priv->allocator = allocator))
    gst_object_ref (allocator);
  priv->params = params;

  return TRUE;

wrong_config:
  {
    GST_WARNING_OBJECT (pool, "invalid config %" GST_PTR_FORMAT, config);
    return FALSE;
  }
no_cmem_allocator:
  {
    GST_WARNING_OBJECT (pool, "GstCeSliceBufferPool need a CMEM allocator");
    return FALSE;
  }
}

static gboolean
mark_meta_pooled (GstBuffer * buffer, GstMeta ** meta, gpointer user_data)
{
  GstBufferPool *pool = user_data;

  GST_DEBUG_OBJECT (pool, "marking meta %p as POOLED in buffer %p", *meta,
      buffer);
  GST_META_FLAG_SET (*meta, GST_META_FLAG_POOLED);
  GST_META_FLAG_SET (*meta, GST_META_FLAG_LOCKED);

  return TRUE;
}

/* The GstBufferPool start function implementation for preallocating 
 * the buffers memory in the pool */
static gboolean
ce_slice_buffer_pool_start (GstBufferPool * pool)
{
  GstCeSliceBufferPool *spool = GST_CE_SLICE_BUFFER_POOL_CAST (pool);
  GstCeSliceBufferPoolPrivate *priv = spool->priv;
  memSlice *slice;
  GstMapInfo info;

  GST_DEBUG_OBJECT (pool, "starting slice buffer pool");

  /* Allocate memory block */
  GST_DEBUG_OBJECT (pool, "allocating memory block of size %d",
      priv->memory_block_size);
  priv->memory =
      gst_allocator_alloc (priv->allocator, priv->memory_block_size,
      &priv->params);
  if (!priv->memory)
    goto fail_alloc;

  if (!gst_memory_map (priv->memory, &info, GST_MAP_READ))
    goto fail_map;
  priv->data = info.data;
  gst_memory_unmap (priv->memory, &info);

  /* First slice with the complete memory block */
  slice = g_malloc0 (sizeof (memSlice));
  slice->start = 0;
  slice->end = priv->memory_block_size;
  slice->size = priv->memory_block_size;

  priv->slices = g_list_append (priv->slices, slice);

  return TRUE;

  /* ERRORS */
fail_alloc:
  {
    GST_WARNING_OBJECT (pool, "failed to allocate memory");
    return FALSE;
  }
fail_map:
  {
    gst_memory_unref (priv->memory);
    priv->memory = NULL;
    GST_WARNING_OBJECT (pool, "failed to map memory");
    return FALSE;
  }
}

/* The GstBufferPool stop function implementation for realeasing
 * the buffers memory in the pool */
static gboolean
ce_slice_buffer_pool_stop (GstBufferPool * pool)
{
  GstCeSliceBufferPool *spool = GST_CE_SLICE_BUFFER_POOL_CAST (pool);
  GstCeSliceBufferPoolPrivate *priv = spool->priv;

  GST_SLICE_POOL_LOCK (spool);

  if (priv->slices
      && ((memSlice *) (priv->slices->data))->size != priv->memory_block_size) {
    GST_WARNING_OBJECT (pool,
        "not all downstream buffers are free... "
        "forcing release, this may cause a segfault");
  }

  if (G_LIKELY (priv->slices)) {
    GList *e = priv->slices;
    /* Merge free memory */
    while (e) {
      g_free (e->data);
      e = g_list_next (e);
    }
    g_list_free (priv->slices);

    priv->slices = NULL;
  }

  if (priv->memory) {
    gst_memory_unref (priv->memory);
    priv->memory = NULL;
  }

  GST_SLICE_POOL_UNLOCK (spool);

  priv->cur_buffers = 0;

  return TRUE;
}

GList *
get_slice (GstCeSliceBufferPool * spool, gint * size)
{
  GstCeSliceBufferPoolPrivate *priv = spool->priv;
  GList *e, *a = NULL;
  memSlice *slice, *max_slice_available = NULL;
  int max_size = 0;

  /* Find free memory */
  GST_DEBUG_OBJECT (spool, "finding free memory");

  GST_SLICE_POOL_LOCK (spool);
  e = priv->slices;
  while (e) {
    slice = (memSlice *) e->data;
    GST_DEBUG_OBJECT (spool, "evaluating free slice from %d to %d",
        slice->start, slice->end);
    if (slice->size >= *size) {
      /* We mark all the memory as buffer at this point
       * to avoid merges while we are using the area
       * Once we know how much memory we actually used, we 
       * update to the real memory size that was used
       */
      slice->start += *size;
      slice->size -= *size;
      a = e;
      goto out;
    }

    if (slice->size > max_size) {
      max_slice_available = slice;
      max_size = slice->size;
      a = e;
    }

    e = g_list_next (e);
  }

  if (slice->start != priv->memory_block_size) {
    if (max_slice_available) {
      GST_WARNING_OBJECT (spool,
          "free memory not found, using our best available free block of size %d... from %d to %d",
          max_slice_available->size, max_slice_available->start,
          max_slice_available->start + max_slice_available->size);

      max_slice_available->start += max_slice_available->size;
      *size = max_slice_available->size;
      max_slice_available->size = 0;
    }
  } else {
    a = NULL;
  }

out:
  {
    GST_SLICE_POOL_UNLOCK (spool);
    return a;
  }
}

/* The GstBufferPool alloc_buffer function implementation to allocate
 * a new buffer that wraps a memory slice */
static GstFlowReturn
ce_slice_buffer_pool_buffer_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstCeSliceBufferPool *spool = GST_CE_SLICE_BUFFER_POOL_CAST (pool);
  GstCeSliceBufferPoolPrivate *priv = spool->priv;
  GstMemory *mem;
  GList *element;
  memSlice *slice;
  gint offset;
  gint size = priv->buffer_size;

  /* Find free buffer */
  element = get_slice (spool, &size);
  if (!element)
    goto no_memory;

  priv->last_slice = slice = (memSlice *) (element->data);
  /* The offset was already reserved, so we need to correct the start */
  offset = slice->start - size;
  mem =
      gst_cmem_new_wrapped (GST_MEMORY_FLAG_NO_SHARE, priv->data + offset,
      size, 0, size, NULL, NULL);
  if (!mem)
    goto no_memory;
  *buffer = gst_buffer_new ();
  gst_buffer_append_memory (*buffer, mem);

  gst_buffer_foreach_meta (*buffer, mark_meta_pooled, pool);

  return GST_FLOW_OK;
no_memory:
  {
    GST_WARNING_OBJECT (pool, "not enough space free on the reserved memory");
    return GST_FLOW_ERROR;
  }
}

static void
ce_slice_buffer_pool_release_buffer (GstBufferPool * pool, GstBuffer * buffer)
{
  GstCeSliceBufferPool *spool = GST_CE_SLICE_BUFFER_POOL_CAST (pool);
  GstCeSliceBufferPoolPrivate *priv = spool->priv;
  GstMapInfo info;
  memSlice *slice, *nslice;
  gint spos, epos, buffer_size;
  GList *e;

  /* keep it around in our queue */
  GST_DEBUG_OBJECT (spool, "released buffer %p", buffer);
  GST_SLICE_POOL_LOCK (spool);

  if (!priv->data || !priv->slices) {
    GST_DEBUG_OBJECT (spool,
        "releasing memory after memory structures were freed");
    /* No need for unlock, since it wasn't taked */
    goto out;
  }

  if (gst_buffer_n_memory (buffer) > 1) {
    GST_DEBUG_OBJECT (spool,
        "buffer was modified or doesn't belong to this buffer pool");
    goto out;
  }

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ))
    goto fail_map;
  spos = info.data - info.memory->offset - priv->data;
  buffer_size = info.memory->maxsize;
  gst_buffer_unmap (buffer, &info);

  epos = spos + buffer_size;

  if (!epos > priv->buffer_size) {
    GST_DEBUG_OBJECT (spool,
        "releasing buffer how ends outside memory boundaries");
    return;
  }

  GST_DEBUG_OBJECT (spool, "releasing memory from %d to %d", spos, epos);
  e = priv->slices;

  /* Merge free memory */
  while (e) {

    slice = (memSlice *) e->data;

    /* Are we contigous to this block? */
    if (slice->start == epos) {
      GST_DEBUG_OBJECT (spool,
          "merging free buffer at beggining free block (%d,%d)",
          slice->start, slice->end);
      /* Merge with current block */
      slice->start -= buffer_size;
      slice->size += buffer_size;
      /* Merge with previous block? */
      if (g_list_previous (e)) {
        nslice = (memSlice *) g_list_previous (e)->data;
        if (nslice->end == slice->start) {
          GST_DEBUG_OBJECT (spool, "closing gaps...");
          nslice->end += slice->size;
          nslice->size += slice->size;
          g_free (slice);
          priv->slices = g_list_delete_link (priv->slices, e);
        }
      }
      goto out;
    }

    if (slice->end == spos) {
      GST_DEBUG_OBJECT (spool,
          "merging free buffer at end of free block (%d,%d)",
          slice->start, slice->end);
      /* Merge with current block */
      slice->end += buffer_size;
      slice->size += buffer_size;
      /* Merge with next block? */
      if (g_list_next (e)) {
        nslice = (memSlice *) g_list_next (e)->data;
        if (nslice->start == slice->end) {
          GST_DEBUG_OBJECT (spool, "closing gaps...");
          slice->end += nslice->size;
          slice->size += nslice->size;
          g_free (nslice);
          priv->slices = g_list_delete_link (priv->slices, g_list_next (e));
        }
      }
      goto out;
    }

    /* Create a new free slice */
    if (slice->start > epos) {
      GST_DEBUG_OBJECT (spool, "creating new free slice %d,%d before %d,%d",
          spos, epos, slice->start, slice->end);
      nslice = g_malloc0 (sizeof (memSlice));
      nslice->start = spos;
      nslice->end = epos;
      nslice->size = buffer_size;
      priv->slices = g_list_insert_before (priv->slices, e, nslice);

      goto out;
    }

    e = g_list_next (e);

  }

  GST_DEBUG_OBJECT (spool, "creating new free slice %d,%d at end of list",
      spos, epos);
  /* We reach the end of the list, so we append the free slice at the 
     end
   */
  nslice = g_malloc0 (sizeof (memSlice));
  nslice->start = spos;
  nslice->end = epos;
  nslice->size = buffer_size;
  priv->slices = g_list_insert_before (priv->slices, NULL, nslice);

out:
  {
    gst_buffer_unref (buffer);
    GST_SLICE_POOL_UNLOCK (spool);
    return;
  }
/* ERRORS */
fail_map:
  {
    GST_WARNING_OBJECT (pool, "invalid buffer");
    gst_buffer_unref (buffer);
    GST_SLICE_POOL_UNLOCK (spool);
    return;
  }

}

static GstFlowReturn
ce_slice_buffer_pool_acquire_buffer (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstCeSliceBufferPool *spool = GST_CE_SLICE_BUFFER_POOL_CAST (pool);
  GstFlowReturn result;

  if (G_UNLIKELY (GST_BUFFER_POOL_IS_FLUSHING (pool)))
    goto flushing;

  /* Allocate buffer */
  GST_LOG_OBJECT (spool, "trying to allocate buffer");
  result = ce_slice_buffer_pool_buffer_alloc (pool, buffer, params);
  if (G_LIKELY (result == GST_FLOW_OK)) {
    GST_LOG_OBJECT (spool, "acquired buffer %p", *buffer);
    goto out;
  }

  if (G_UNLIKELY (result != GST_FLOW_EOS))
    /* something went wrong, return error */
    goto out;

  /* 
   * TODO
   * Should wait? 
   * Should use buffer queue?
   */

out:
  return result;
  /* ERRORS */
flushing:
  {
    GST_DEBUG_OBJECT (spool, "we are flushing");
    return GST_FLOW_FLUSHING;
  }
}

/**
 * gst_ce_slice_buffer_pool_new:
 *
 * Creates a new #GstCeSliceBufferPool instance. This bufferpool reserves
 * a single memory block with size corresponding to the maximum buffers 
 * requested. For each buffer acquired with the bufferpool a slice of the 
 * reserved memory is used. The buffer can be resized after usage
 * (like encodification) returning the unused memory.
 *
 * Returns: (transfer full): a new #GstCeSliceBufferPool instance
 */
GstBufferPool *
gst_ce_slice_buffer_pool_new (void)
{
  GstBufferPool *result;

  result = g_object_newv (GST_TYPE_CE_SLICE_BUFFER_POOL, 0, NULL);
  GST_DEBUG_OBJECT (result, "created new slice buffer pool");

  return result;
}

/**
 * gst_ce_slice_buffer_resize:
 * @pool: a #GstCeSliceBufferPool
 * @buffer: buffer to resize.
 * @size: new buffer size.
 * 
 * Resizes the @buffer to @size. Returns the unused memory to the main
 * memory block and sets the buffer memory maxsize and size to the 
 * given @size.
 *
 * Returns: FALSE if couldn't resize the buffer
 */

gboolean
gst_ce_slice_buffer_resize (GstCeSliceBufferPool * spool, GstBuffer * buffer,
    gint size)
{
  GstCeSliceBufferPoolPrivate *priv;
  GstMapInfo info;
  memSlice *slice;
  GList *element;
  gint spos, epos, buffer_size;
  gint unused, align_size;
  gsize align;

  g_return_val_if_fail (GST_IS_CE_SLICE_BUFFER_POOL (spool), FALSE);
  g_return_val_if_fail (size < spool->priv->buffer_size, FALSE);
  g_return_val_if_fail (spool->priv->memory, FALSE);

  priv = spool->priv;
  align = priv->params.align;

  align_size = (size & ~align) + (align + 1);

  GST_SLICE_POOL_LOCK (spool);

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ))
    goto fail;

  spos = info.data - info.memory->offset - priv->data;
  buffer_size = info.memory->maxsize;
  epos = spos + buffer_size;

  /* Search for the buffer memory slice location and adjust the start 
   * of the next slice */
  element = priv->slices;
  while (element) {
    slice = (memSlice *) element->data;
    if (slice->start == epos) {
      unused = buffer_size - align_size;
      GST_DEBUG_OBJECT (spool, "adjusting slice start from %d to %d",
          slice->start, slice->start - unused);
      slice->start -= unused;
      slice->size += unused;
      if (slice->size == 0) {
        g_free (slice);
        priv->slices = g_list_delete_link (priv->slices, element);
      }
      break;
    }
    element = g_list_next (element);
  }

  if (element) {
    GST_DEBUG_OBJECT (spool, "resizing buffer %p", buffer);
    info.memory->maxsize = align_size;
    gst_buffer_unmap (buffer, &info);
    gst_buffer_set_size (buffer, size);
  } else {
    GST_WARNING_OBJECT (spool, "slice not found");
    goto fail;
  }

  GST_SLICE_POOL_UNLOCK (spool);
  return TRUE;
/* ERRORS */
fail:
  {
    GST_WARNING_OBJECT (spool, "invalid buffer");
    GST_SLICE_POOL_UNLOCK (spool);
    return FALSE;
  }
}
