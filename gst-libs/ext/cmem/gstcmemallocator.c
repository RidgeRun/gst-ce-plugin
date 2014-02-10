/* GStreamer
 * Copyright (C) 2013 RidgeRun, LLC (http://www.ridgerun.com)

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 2 of the License, or
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

#include <string.h>
#include <xdc/std.h>
#include <ti/sdo/ce/CERuntime.h>
#include <ti/sdo/ce/osal/Memory.h>

#include "gstcmemallocator.h"

GST_DEBUG_CATEGORY_EXTERN (GST_CAT_PERFORMANCE);
GST_DEBUG_CATEGORY_EXTERN (GST_CAT_MEMORY);

GST_DEBUG_CATEGORY_STATIC (gst_cmem_debug);
#define GST_CAT_DEFAULT gst_cmem_debug

static GstAllocator *_cmem_allocator;

typedef struct
{
  GstMemory mem;
  guint8 *data;
  guint32 alloc_size;
  Memory_AllocParams alloc_params;
  /*Parameters used by wrapped memory */
  gpointer user_data;
  GDestroyNotify notify;
} GstMemoryContig;

typedef struct
{
  GstAllocator parent;
} GstCMemAllocator;

typedef struct
{
  GstAllocatorClass parent_class;
} GstCMemAllocatorClass;

G_DEFINE_TYPE (GstCMemAllocator, gst_cmem_allocator, GST_TYPE_ALLOCATOR);

/* initialize the fields */
static void
_cmem_init (GstMemoryContig * mem, GstMemoryFlags flags, GstMemory * parent,
    gsize alloc_size, gpointer data, gsize maxsize, gsize align, gsize offset,
    gsize size, Memory_AllocParams * alloc_params, gpointer user_data,
    GDestroyNotify notify)
{
  gst_memory_init (GST_MEMORY_CAST (mem),
      flags, _cmem_allocator, parent, maxsize, align, offset, size);

  mem->alloc_size = alloc_size;
  mem->data = data;
  if (alloc_params)
    mem->alloc_params = *alloc_params;
  mem->user_data = user_data;
  mem->notify = notify;
}

/* allocate the memory and structure in one block */
static GstMemoryContig *
_cmem_new_mem_block (gsize maxsize, gsize align, gsize offset, gsize size)
{
  GstMemoryContig *mem;
  Memory_AllocParams params;
  guint8 *data;

  GST_DEBUG ("new cmem block");
  mem = g_slice_alloc (sizeof (GstMemoryContig));
  if (!mem)
    goto fail_alloc;

  params = Memory_DEFAULTPARAMS;
  params.type = Memory_CONTIGPOOL;
  params.flags = Memory_CACHED;
  params.align = (UInt) align;

  data = NULL;

  if (size > 0) {
    data = (guint8 *) Memory_alloc (maxsize, &params);
    if (!data)
      goto fail_alloc;
  }

  _cmem_init (mem, 0, NULL, maxsize, data, maxsize,
      align, offset, size, &params, NULL, NULL);

  GST_DEBUG ("succesfull CMEM allocation");

  return mem;

fail_alloc:
  GST_ERROR ("failed CMEM allocation");
  if (mem)
    g_slice_free1 (sizeof (GstMemoryContig), mem);
  return NULL;
}

/**
 * _cmem_map:
 * 
 * The implementation of the GstMemoryMapFunction.
 */
static gpointer
_cmem_map (GstMemoryContig * mem, GstMapFlags flags)
{
  g_return_val_if_fail (mem, NULL);

  if (flags & GST_MAP_READ) {
    GST_DEBUG ("invalidate cache for memory %p", mem);
    Memory_cacheInv (mem->data, mem->mem.maxsize);
  }
  return mem->data;
}

/**
 * _cmem_unmap:
 * 
 * The implementation of the GstMemoryUnmapFunction.
 */
static gboolean
_cmem_unmap (GstMemoryContig * mem)
{
  g_return_val_if_fail (mem, FALSE);

  GST_DEBUG ("write-back cache for memory %p", mem);
  Memory_cacheWb (mem->data, mem->mem.maxsize);
  return TRUE;
}

/**
 * _cmem_copy:
 * 
 * The implementation of the GstMemoryCopyFunction.
 */
static GstMemoryContig *
_cmem_copy (GstMemoryContig * mem, gssize offset, gsize size)
{
  GstMemoryContig *copy;
  gsize align;

  g_return_val_if_fail (mem, NULL);

  GST_LOG ("Copy CMEM buffer %p", mem->data);

  if (size == -1)
    size = mem->mem.size > offset ? mem->mem.size - offset : 0;

  /*
   * GstAllocationParams have an alignment that is a bitmask
   * so that align + 1 equals the amount of bytes to align to.
   */
  align = mem->mem.align + 1;

  copy =
      _cmem_new_mem_block (mem->mem.maxsize, align,
      mem->mem.offset + offset, size);

  if (!copy)
    goto out;

  GST_CAT_DEBUG (GST_CAT_PERFORMANCE,
      "memcpy %" G_GSIZE_FORMAT " memory %p -> %p", mem->mem.maxsize, mem,
      copy);

  memcpy (copy->data, mem->data, mem->mem.maxsize);

out:
  return copy;
}

/**
 * _cmem_share:
 * 
 * The implementation of the GstMemoryShareFunction.
 */
static GstMemoryContig *
_cmem_share (GstMemoryContig * mem, gssize offset, gsize size)
{
  GstMemoryContig *sub;
  GstMemory *parent;

  g_return_val_if_fail (mem, NULL);

  GST_DEBUG ("%p: share %" G_GSSIZE_FORMAT " %" G_GSIZE_FORMAT, mem, offset,
      size);

  /* find the real parent */
  if ((parent = mem->mem.parent) == NULL)
    parent = (GstMemory *) mem;

  if (size == -1)
    size = mem->mem.size - offset;

  sub = g_slice_alloc (sizeof (GstMemoryContig));
  if (mem == NULL)
    return NULL;

  /* the shared memory is always readonly */
  _cmem_init (sub, GST_MINI_OBJECT_FLAGS (parent) |
      GST_MINI_OBJECT_FLAG_LOCK_READONLY, parent, 0, mem->data,
      mem->mem.maxsize, mem->mem.align, mem->mem.offset + offset,
      size, &mem->alloc_params, NULL, NULL);

  return sub;
}

/**
 * _cmem_is_span:
 * 
 * The implementation of the GstMemoryIsSpanFunction.
 */
static gboolean
_cmem_is_span (GstMemoryContig * mem1, GstMemoryContig * mem2, gsize * offset)
{
  g_return_val_if_fail (mem1, FALSE);
  g_return_val_if_fail (mem2, FALSE);

  if (offset) {
    GstMemoryContig *parent;

    parent = (GstMemoryContig *) mem1->mem.parent;

    *offset = mem1->mem.offset - parent->mem.offset;
  }

  /* and memory is contiguous */
  return mem1->data + mem1->mem.offset + mem1->mem.size ==
      mem2->data + mem2->mem.offset;
}

/**
 * _cmem_alloc:
 * 
 * The implementation of the GstAllocatorClass alloc() function.
 */
static GstMemory *
_cmem_alloc (GstAllocator * allocator, gsize size, GstAllocationParams * params)
{
  gsize maxsize = size + params->prefix + params->padding;
  gsize align;

  GST_DEBUG ("allocating CMEM buffer: size %d, prefix %d, padding %d",
      size, params->prefix, params->padding);

  /*
   * GstAllocationParams have an alignment that is a bitmask
   * so that align + 1 equals the amount of bytes to align to.
   */
  align = params->align + 1;

  return (GstMemory *) _cmem_new_mem_block (maxsize, align,
      params->prefix, size);
}

/**
 * _cmem_free:
 * 
 * The implementation of the GstAllocatorClass free() function.
 */
static void
_cmem_free (GstAllocator * allocator, GstMemory * mem)
{
  GstMemoryContig *cmem = (GstMemoryContig *) mem;

  GST_CAT_DEBUG (gst_cmem_debug, "cmem free memory %p", cmem);

  if (cmem->notify)
    cmem->notify (cmem->user_data);

  if (cmem->alloc_size) {
    GST_DEBUG ("free memory %p", cmem->data);
    Memory_cacheWb (cmem->data, cmem->alloc_size);
    Memory_free (cmem->data, cmem->alloc_size, &cmem->alloc_params);
  }
  g_slice_free1 (sizeof (GstMemoryContig), mem);
}

static void
gst_cmem_allocator_class_init (GstCMemAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class;

  allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = _cmem_alloc;
  allocator_class->free = _cmem_free;
}

static void
gst_cmem_allocator_init (GstCMemAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  GST_CAT_DEBUG (GST_CAT_MEMORY, "init allocator %p", allocator);

  alloc->mem_type = GST_ALLOCATOR_CMEM;
  alloc->mem_map = (GstMemoryMapFunction) _cmem_map;
  alloc->mem_unmap = (GstMemoryUnmapFunction) _cmem_unmap;
  alloc->mem_copy = (GstMemoryCopyFunction) _cmem_copy;
  alloc->mem_share = (GstMemoryShareFunction) _cmem_share;
  alloc->mem_is_span = (GstMemoryIsSpanFunction) _cmem_is_span;

  CERuntime_init ();
}

/**
 * gst_cmem_init:
 * 
 * Registers a new memory allocator called "ContiguousMemory". The
 * allocator is capable to get contiguos memory (CMEM) using the
 * Codec Engine API.
 */
void
gst_cmem_init (void)
{
  GST_DEBUG_CATEGORY_INIT (gst_cmem_debug, "cmem", 0, "CMEM allocator");
  _cmem_allocator = g_object_new (gst_cmem_allocator_get_type (), NULL);
  if (!_cmem_allocator)
    GST_ERROR ("failed to create gst_cmem_allocator object");
  gst_allocator_register (GST_ALLOCATOR_CMEM, _cmem_allocator);
  GST_DEBUG ("cmem memory allocator registered!");
}

/**
 * gst_cmem_cache_inv:
 * @data: pointer to the buffer data.
 * @size: size of the memory region to invalite.
 * 
 * Do a cache invalidate of the block of @size pointed to by @data
 */
void
gst_cmem_cache_inv (guint8 * data, gint size)
{
  g_return_if_fail (data);

  if (size > 0)
    Memory_cacheInv (data, size);
}

/**
 * gst_cmem_cache_inv:
 * @data: pointer to the buffer data.
 * @size: size of the memory region to invalite.
 * 
 * Do a cache write back of the block of @size pointed to by @data
 */
void
gst_cmem_cache_wb (guint8 * data, gint size)
{
  g_return_if_fail (data);

  if (size > 0)
    Memory_cacheWb (data, size);
}

/**
 * gst_cmem_cache_inv:
 * @data: pointer to the buffer data.
 * @size: size of the memory region to invalite.
 * 
 * Write back and invalidate a region of cache 
 */
void
gst_cmem_cache_wb_inv (guint8 * data, gint size)
{
  g_return_if_fail (data);

  if (size > 0)
    Memory_cacheWbInv (data, size);
}


/**
 * gst_memory_new_wrapped:
 * @flags: #GstMemoryFlags
 * @data: data to wrap
 * @maxsize: allocated size of @data
 * @offset: offset in @data
 * @size: size of valid data
 * @user_data: user_data
 * @notify: called with @user_data when the memory is freed
 * 
 * Allocate a new cmem memory block that wraps the given @data. The 
 * @data is not freed when the memory is freed.
 *
 * Returns: a new #GstMemory.
 */
GstMemory *
gst_cmem_new_wrapped (GstMemoryFlags flags, gpointer data,
    gsize maxsize, gsize offset, gsize size, gpointer user_data,
    GDestroyNotify notify)
{
  GstMemoryContig *mem;

  GST_LOG ("new wrapped CMEM memory");

  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (offset + size <= maxsize, NULL);

  mem = g_slice_alloc (sizeof (GstMemoryContig));
  if (!mem)
    return NULL;

  _cmem_init (mem, flags, NULL, 0, data, maxsize, 0, offset,
      size, NULL, user_data, notify);

  return (GstMemory *) mem;
}
