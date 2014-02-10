/*
 * gstceutils.h
 *
 * Copyright (C) 2013 RidgeRun, LLC (http://www.ridgerun.com)
 *
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

#ifndef __GST_CE_UTILS_H__
#define __GST_CE_UTILS_H__

#include <gst/gst.h>

G_BEGIN_DECLS 

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)));
#define CODEC_ENGINE "codecServer"

typedef struct _GstCeContigBufMeta GstCeContigBufMeta;

/**
 * GstCeMeta:
 * @meta: parent #GstMeta
 * @addr: virtual address of the buffer
 * @size: size of the region
 *
 * Metadata
 * Extra buffer metadata indicating a contiguous buffer registered
 * with Codec Engine.
 */
struct _GstCeContigBufMeta
{
  GstMeta meta;

  guint32 addr;
  guint32 size;
};

gboolean gst_ce_is_buffer_contiguous (GstBuffer * buffer);
GType gst_ce_contig_buf_meta_api_get_type (void);
const GstMetaInfo *gst_ce_contig_buf_meta_get_info (void);
#define GST_CE_CONTIG_BUF_META_API_TYPE (gst_ce_meta_api_get_type())
#define GST_CE_CONTIG_BUF_META_GET(buf) ((GstCeContigBufMeta *)gst_buffer_get_meta(buf, gst_ce_contig_buf_meta_api_get_type()))
#define GST_CE_CONTIG_BUF_META_ADD(buf) ((GstCeContigBufMeta *)gst_buffer_add_meta(buf, gst_ce_contig_buf_meta_get_info(), NULL))

G_END_DECLS
#endif /*__GST_CE_UTILS_H__*/
