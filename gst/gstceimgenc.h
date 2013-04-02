/*
 * gstceimgenc.h
 *
 * Copyright (C) 2013 RidgeRun, LLC (http://www.ridgerun.com)
 * Author: Carlos Gomez Viquez <carlos.gomez@ridgerun.com>
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

#ifndef __GST_CE_IMGENC_H__
#define __GST_CE_IMGENC_H__

#include <xdc/std.h>
#include <ti/sdo/ce/Engine.h>
#include <ti/sdo/ce/image1/imgenc1.h>
#include <ti/sdo/ce/video1/videnc1.h>
#include <gst/video/gstvideoencoder.h>
#include "gstceutils.h"

G_BEGIN_DECLS

typedef struct _GstCEImgEnc GstCEImgEnc;

struct _GstCEImgEnc
{
  GstVideoEncoder parent;
  gboolean first_buffer;
  
  gint32 outbuf_size;
  GstVideoCodecState *input_state;

  /* Handle to the CMEM allocator */
  GstAllocator *allocator;
  GstAllocationParams params;

  /* Handle to the Codec Engine */
  Engine_Handle engine_handle;

  /* Handle to the Codec */
  IMGENC1_Handle codec_handle;
  IMGENC1_Params *codec_params;
  IMGENC1_DynamicParams *codec_dyn_params;
  IVIDEO1_BufDescIn inbuf;
  XDM_BufDesc outbuf;

  /* Codec Private Data */
  void *codec_private;
};

typedef struct _GstCEImgEncClass GstCEImgEncClass;

struct _GstCEImgEncClass
{
  GstVideoEncoderClass parent_class;

  GstCECodecData *codec;
  GstPadTemplate *srctempl, *sinktempl;
  GstCaps *sinkcaps;
};

#define GST_TYPE_CEIMGENC \
  (gst_ceimgenc_get_type())
#define GST_CEIMGENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CEIMGENC,GstCEImgEnc))
#define GST_CEIMGENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CEIMGENC,GstCEImgEncClass))
#define GST_IS_CEIMGENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CEIMGENC))
#define GST_IS_CEIMGENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CEIMGENC))

GType gst_ceimgenc_get_type (void);

G_END_DECLS
#endif /* __GST_CE_IMGENC_H__ */
