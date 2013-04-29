/*
 * gstceaudenc.h
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

#ifndef __GST_CE_AUDENC_H__
#define __GST_CE_AUDENC_H__

#include <gst/audio/gstaudioencoder.h>

#include <xdc/std.h>
#include <ti/sdo/ce/Engine.h>
#include <ti/sdo/ce/audio1/audenc1.h>

#include "gstceutils.h"

G_BEGIN_DECLS typedef struct _GstCEAudEnc GstCEAudEnc;
typedef struct _GstCEAudEncClass GstCEAudEncClass;
typedef struct _GstCEAudEncPrivate GstCEAudEncPrivate;

struct _GstCEAudEnc
{
  GstAudioEncoder parent;

  /* Handle to the Codec */
  AUDENC1_Handle codec_handle;
  AUDENC1_Params *codec_params;
  AUDENC1_DynamicParams *codec_dyn_params;

  /*< private > */
  GstCEAudEncPrivate *priv;

  gpointer _gst_reserved[GST_PADDING_LARGE];
};


/**
 * GstCEAudEncClass
 * @parent_class:   Element parent class
 * @codec_name:     The name of the codec
 * @reset:          Optional.
 *                  Allows subclass to set default values of properties and
 *                  reset the element resources. Called when the element is
 *                  initialized and when the element stops processing.
 * @set_src_caps:   Optional.
 *                  Allows subclass to be notified of the actual src caps
 *                  and be able to propose custom src caps and codec data.
 * @pre_process:    Optional.
 *                  Called right before the base class will start the encoding 
 *                  process. Allows delayed configuration and dynamic properties
 *                  be performed in this method.
 * @post_process:   Optional.
 *                  Called after the base class finished the encoding 
 *                  process. Allows output buffer transformations.
 * 
 * Subclasses can override any of the available virtual methods or not, as
 * needed. At minimum @codec_name shoud be filled.
 */
struct _GstCEAudEncClass
{
  GstAudioEncoderClass parent_class;

  /*< public > */
  const gchar *codec_name;
 gint samples;

  /* virtual methods for subclasses */
  void (*reset) (GstCEAudEnc * ceaudenc);
    gboolean (*set_src_caps) (GstCEAudEnc * ceaudenc, GstAudioInfo * info,
      GstCaps ** src_caps, GstBuffer ** codec_data);

    gboolean (*pre_process) (GstCEAudEnc * ceaudenc, GstBuffer * input_buffer);
    gboolean (*post_process) (GstCEAudEnc * ceaudenc,
      GstBuffer * output_buffer);

  /*< private > */
  gpointer _gst_reserved[GST_PADDING_LARGE];
};

#define GST_TYPE_CEAUDENC \
  (gst_ceaudenc_get_type())
#define GST_CEAUDENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CEAUDENC,GstCEAudEnc))
#define GST_CEAUDENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CEAUDENC,GstCEAudEncClass))
#define GST_IS_CEAUDENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CEAUDENC))
#define GST_IS_CEAUDENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CEAUDENC))

GType gst_ceaudenc_get_type (void);

G_END_DECLS
#endif /* __GST_CE_AUDENC_H__ */
