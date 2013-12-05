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

#ifndef _GST_CMEM_ALLOCATOR_H_
#define _GST_CMEM_ALLOCATOR_H_

#include <gst/gst.h>
#include <gst/gstmemory.h>

G_BEGIN_DECLS
#define GST_ALLOCATOR_CMEM "ContiguousMemory"
#define GST_TYPE_CMEM_ALLOCATOR \
  (gst_cmem_allocator_get_type())
#define GST_IS_CMEM_ALLOCATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CMEM_ALLOCATOR))
    GType gst_cmem_allocator_get_type (void);

void gst_cmem_init (void);
void gst_cmem_cache_inv (guint8 * data, gint size);
void gst_cmem_cache_wb (guint8 * data, gint size);
void gst_cmem_cache_wb_inv (guint8 * data, gint size);

GstMemory *gst_cmem_new_wrapped (GstMemoryFlags flags, gpointer data,
    gsize maxsize, gsize offset, gsize size, gpointer user_data,
    GDestroyNotify notify);

G_END_DECLS
#endif /*_GST_CMEM_ALLOCATOR_H_*/
