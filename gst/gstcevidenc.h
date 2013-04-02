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

#include <gst/video/gstvideoencoder.h>
#include <xdc/std.h>
#include <ti/sdo/ce/Engine.h>
#include <ti/sdo/ce/video1/videnc1.h>

#include "gstceutils.h"

G_BEGIN_DECLS 

typedef struct _GstCEVidEnc GstCEVidEnc;
typedef struct _GstCEVidEncClass GstCEVidEncClass;
typedef struct _GstCEVidEncPrivate GstCEVidEncPrivate;

struct _GstCEVidEnc
{
  GstVideoEncoder parent;

  /* Handle to the Codec */
  VIDENC1_Handle codec_handle;
  VIDENC1_Params *codec_params;
  VIDENC1_DynamicParams *codec_dyn_params;
  
  /*< private >*/
  GstCEVidEncPrivate *priv;

  gpointer  _gst_reserved[GST_PADDING_LARGE];
};



struct _GstCEVidEncClass
{
  GstVideoEncoderClass parent_class;

  GstCECodecData *codec;
  GstPadTemplate *srctempl, *sinktempl;
  GstCaps *sinkcaps;

  /*< public > */
  const gchar *codec_name;
  
  /* virtual methods for subclasses */

  /* initialize resources */
  void (*reset) (GstCEVidEnc * cevidenc);

  /* define element src caps */
  gboolean (*set_src_caps) (GstCEVidEnc *cevidenc, GstCaps **, GstBuffer ** codec_data);

  /* process before and after the encoding */
  gboolean (*pre_process) (GstCEVidEnc *cevidenc, GstBuffer *input_buffer);
  gboolean (*post_process) (GstCEVidEnc *cevidenc, GstBuffer *output_buffer);

  /*< private > */
  gpointer _gst_reserved[GST_PADDING_LARGE];
};

#define GST_TYPE_CEVIDENC \
  (gst_cevidenc_get_type())
#define GST_CEVIDENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CEVIDENC,GstCEVidEnc))
#define GST_CEVIDENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CEVIDENC,GstCEVidEncClass))
#define GST_IS_CEVIDENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CEVIDENC))
#define GST_IS_CEVIDENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CEVIDENC))

GType gst_cevidenc_get_type (void);

gboolean gst_cevidenc_get_header (GstCEVidEnc * cevidenc, 
                                  GstBuffer ** buf,
                                  gint *header_size);
                                  

G_END_DECLS
#endif /* __GST_CE_VIDENC_H__ */
