/*
 * gstceutils.c
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

#include <xdc/std.h>
#include <ti/sdo/ce/osal/Memory.h>
#include "gstceutils.h"

/* A number of function prototypes are given so we can refer to them later. */
gboolean gst_ce_contig_buf_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer);
void gst_ce_contig_buf_meta_free (GstMeta * meta, GstBuffer * buffer);

GType
gst_ce_contig_buf_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] = { "memory", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstCEMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

/**
 * gst_ce_contig_buf_meta_init:
 * 
 * The implementation of the GstMetaInitFunction, called
 * when the metadata is added to a buffer.
 */
gboolean
gst_ce_contig_buf_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  gint32 phys, virt;
  GstMapInfo info;
  gboolean is_contiguous = FALSE;
  GstCEContigBufMeta *cemeta;

  cemeta = (GstCEContigBufMeta *) meta;

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ))
    goto out;

  virt = (guint32) info.data;

  phys = Memory_getBufferPhysicalAddress (info.data, info.size,
      (Bool *) & is_contiguous);

  if (!is_contiguous)
    goto out;

  /*Registering contiguous buffer to Codec Engine */
  Memory_registerContigBuf (virt, info.size, phys);
  cemeta->addr = virt;
  cemeta->size = info.size;

  gst_buffer_unmap (buffer, &info);

  GST_DEBUG ("Init CE meta %d", cemeta->addr);

out:
  return is_contiguous;
}

/**
 * gst_ce_contig_buf_meta_free:
 * 
 * The implementation of the GstMetaFreeFunction, called
 * when the metadata is removed from a buffer.
 */
void
gst_ce_contig_buf_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstCEContigBufMeta *cemeta;

  cemeta = (GstCEContigBufMeta *) meta;

  Memory_unregisterContigBuf (cemeta->addr, cemeta->size);
  GST_DEBUG ("Free CE meta %d", cemeta->addr);
}

const GstMetaInfo *
gst_ce_contig_buf_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter (&meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (gst_ce_contig_buf_meta_api_get_type (),
        "GstCEContigBufMeta",
        sizeof (GstCEContigBufMeta),
        (GstMetaInitFunction) gst_ce_contig_buf_meta_init,
        (GstMetaFreeFunction) gst_ce_contig_buf_meta_free,
        (GstMetaTransformFunction) NULL);
    g_once_init_leave (&meta_info, meta);
  }
  return meta_info;
}
