/*
 * gstceslicepool.h
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

#ifndef __GST_CE_SLICE_POOL_H__
#define __GST_CE_SLICE_POOL_H__

#include <gst/gst.h>
#include "gstce.h"

G_BEGIN_DECLS
/* slice bufferpool */
typedef struct _GstCESliceBufferPool GstCESliceBufferPool;
typedef struct _GstCESliceBufferPoolClass GstCESliceBufferPoolClass;
typedef struct _GstCESliceBufferPoolPrivate GstCESliceBufferPoolPrivate;

#define GST_TYPE_CE_SLICE_BUFFER_POOL      (gst_ce_slice_buffer_pool_get_type())
#define GST_IS_CE_SLICE_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_CE_SLICE_BUFFER_POOL))
#define GST_CE_SLICE_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_CE_SLICE_BUFFER_POOL, GST_TYPE_CE_SLICE_BUFFER_POOL))
#define GST_CE_SLICE_BUFFER_POOL_CAST(obj) ((GstCESliceBufferPool*)(obj))

struct _GstCESliceBufferPool
{
  GstBufferPool bufferpool;

  GstCESliceBufferPoolPrivate *priv;
};

struct _GstCESliceBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GType gst_ce_slice_buffer_pool_get_type (void);
GstBufferPool *gst_ce_slice_buffer_pool_new (void);
gboolean gst_ce_slice_buffer_resize (GstCESliceBufferPool * spool,
    GstBuffer * buffer, gint size);

G_END_DECLS
#endif /*__GST_CE_SLICE_POOL_H__*/
