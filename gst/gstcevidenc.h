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

#include <gst/video/gstvideoencoder.h>
#include "gstceutils.h"

typedef struct _GstCEVidEnc GstCEVidEnc;

struct _GstCEVidEnc
{
  GstVideoEncoder parent;
  /*Properties*/
  gint out_buffer_size;
  gboolean copy_output;
  
  /* Video Data */
  gint fps_num;
  gint fps_den;
  gint height;
  gint width;
  gint pitch;
  gint par_num;
  gint par_den;
  gint bpp;
  GstVideoFormat pix_format;
  GstClockTime avg_duration;

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
