/*
 * gstcevidenc.h
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

#ifndef __GST_CE_VIDENC_H__
#define __GST_CE_VIDENC_H__

G_BEGIN_DECLS
#include <xdc/std.h>
#include <ti/sdo/ce/Engine.h>
#include <ti/sdo/ce/video1/videnc1.h>
#include <gst/video/gstvideoencoder.h>
#include "gstceutils.h"
typedef struct _GstCEVidEnc GstCEVidEnc;

struct _GstCEVidEnc
{
  GstVideoEncoder parent;
  gboolean first_buffer;

  /* Video Data */
  gint fps_num;
  gint fps_den;
  gint par_num;
  gint par_den;
  gint bpp;

  gint32 outbuf_size;
  GstVideoFormat video_format;
  GstVideoCodecState *input_state;

  /* Handle to the CMEM allocator */
  GstAllocator *allocator;
  GstAllocationParams alloc_params;

  /* Handle to the Codec Engine */
  Engine_Handle engine_handle;

  /* Handle to the Codec */
  VIDENC1_Handle codec_handle;
  VIDENC1_Params *codec_params;
  VIDENC1_DynamicParams *codec_dyn_params;
  IVIDEO1_BufDescIn inbuf_desc;
  XDM_BufDesc outbuf_desc;

  /* Codec Private Data */
  void *codec_private;
};

typedef struct _GstCEVidEncClass GstCEVidEncClass;

struct _GstCEVidEncClass
{
  GstVideoEncoderClass parent_class;

  GstCECodecData *codec;
  GstPadTemplate *srctempl, *sinktempl;
  GstCaps *sinkcaps;
};

#define GST_TYPE_CEVIDENC \
  (gst_ffmpegvidenc_get_type())
#define GST_CEVIDENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CEVIDENC,GstCEVidEnc))
#define GST_CEVIDENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CEVIDENC,GstCEVidEncClass))
#define GST_IS_CEVIDENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CEVIDENC))
#define GST_IS_CEVIDENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CEVIDENC))

G_END_DECLS
#endif /* __GST_CE_VIDENC_H__ */
