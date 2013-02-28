/*
 * gstcevidenc.c
 *
 * Copyright (C) 2013 RidgeRun, LLC (http://www.ridgerun.com)
 *
 * Based on gst-libav plugin
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <string.h>
/* for stats file handling */
#include <stdio.h>
#include <glib/gstdio.h>
#include <errno.h>

#include <gst/gst.h>
#include <gst/video/gstvideometa.h>

#include "gstce.h"
#include "gstcevidenc.h"


enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_OUTPUT_BUF_SIZE,
  PROP_COPY_OUTPUT,
};

/* A number of function prototypes are given so we can refer to them later. */
static void gst_cevidenc_class_init (GstCEVidEncClass * klass);
static void gst_cevidenc_base_init (GstCEVidEncClass * klass);
static void gst_cevidenc_init (GstCEVidEnc * cevidenc);
static void gst_cevidenc_finalize (GObject * object);

static gboolean gst_cevidenc_open (GstVideoEncoder * encoder);
static gboolean gst_cevidenc_close (GstVideoEncoder * encoder);
static gboolean gst_cevidenc_start (GstVideoEncoder * encoder);
static gboolean gst_cevidenc_stop (GstVideoEncoder * encoder);
static GstFlowReturn gst_cevidenc_finish (GstVideoEncoder * encoder);
static gboolean gst_cevidenc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static gboolean gst_cevidenc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);
static GstFlowReturn gst_cevidenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);

static void gst_cevidenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_cevidenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

#define GST_CEENC_PARAMS_QDATA g_quark_from_static_string("ceenc-params")

static GstElementClass *parent_class = NULL;


static void
gst_cevidenc_base_init (GstCEVidEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstCECodecData *codec;
  GstPadTemplate *srctempl = NULL, *sinktempl = NULL;
  GstCaps *srccaps = NULL, *sinkcaps = NULL;
  gchar *longname, *description;

  codec = (GstCECodecData *)
      g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass), GST_CEENC_PARAMS_QDATA);

  g_assert (codec != NULL);

  /* construct the element details struct */
  longname = g_strdup_printf ("CE %s encoder", codec->long_name);
  description = g_strdup_printf ("CE %s encoder", codec->name);
  gst_element_class_set_metadata (element_class, longname,
      "Codec/Encoder/Video", description,
      "Melissa Montero <melissa.montero@ridgerun.com>");
  g_free (longname);
  g_free (description);

  if (!codec->src_caps) {
    GST_WARNING ("Couldn't get source caps for encoder '%s'", codec->name);
    srccaps = gst_caps_new_empty_simple ("unknown/unknown");
  } else {
    srccaps = gst_static_caps_get (codec->src_caps);
  }

  if (!codec->sink_caps) {
    GST_WARNING ("Couldn't get source caps for encoder '%s' using "
        "video/x-raw", codec->name);
    sinkcaps = gst_caps_new_empty_simple ("video/x-raw");
  } else {
    sinkcaps = gst_static_caps_get (codec->sink_caps);
  }

  /* pad templates */
  sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, sinkcaps);
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, srccaps);

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);

  klass->codec = codec;
  klass->srctempl = srctempl;
  klass->sinktempl = sinktempl;
  klass->sinkcaps = NULL;

  return;
}

static void
gst_cevidenc_class_init (GstCEVidEncClass * klass)
{
  GObjectClass *gobject_class;
  GstVideoEncoderClass *venc_class;

  gobject_class = (GObjectClass *) klass;
  venc_class = (GstVideoEncoderClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_cevidenc_set_property;
  gobject_class->get_property = gst_cevidenc_get_property;

  g_object_class_install_property (gobject_class, PROP_OUTPUT_BUF_SIZE,
      g_param_spec_int ("output-buffer-size",
          "Size of the output buffer",
          "Size of the output buffer", 0, G_MAXINT32, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_COPY_OUTPUT,
      g_param_spec_boolean ("copy-output",
          "Copy the output buffers",
          "Boolean that set if the output buffers should be copied into "
          "standard gst buffers", FALSE, G_PARAM_READWRITE));

  /*$ 
   * TODO: 
   * Pending to add videnc1 properties
   */

  /* Register additional properties, dependent on the exact CODEC */
  if (klass->codec->install_properties) {
    klass->codec->install_properties (gobject_class);
  }

  venc_class->open = gst_cevidenc_open;
  venc_class->close = gst_cevidenc_close;
  venc_class->start = gst_cevidenc_start;
  venc_class->stop = gst_cevidenc_stop;
  venc_class->finish = gst_cevidenc_finish;
  venc_class->handle_frame = gst_cevidenc_handle_frame;
  venc_class->set_format = gst_cevidenc_set_format;
  venc_class->propose_allocation = gst_cevidenc_propose_allocation;

  gobject_class->finalize = gst_cevidenc_finalize;
}

static void
gst_cevidenc_init (GstCEVidEnc * cevidenc)
{
  GstCEVidEncClass *klass = (GstCEVidEncClass *) G_OBJECT_GET_CLASS (cevidenc);

  cevidenc->out_buffer_size = 0;
  cevidenc->copy_output = FALSE;

  /* Allow the codec to allocate any private data it may require 
   * and set defaults */
  if (klass->codec->setup)
    klass->codec->setup ((GObject *) cevidenc);
}

static void
gst_cevidenc_finalize (GObject * object)
{

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static gboolean
gst_cevidenc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  GstCaps *allowed_caps;
  GstVideoCodecState *output_format;
  gint i, bpp = 0;

  GstCEVidEnc *cevidenc = (GstCEVidEnc *) encoder;

  GST_DEBUG_OBJECT (cevidenc, "Extracting common video information");
  /* fetch pix_fmt, fps, par, width, height... */



  cevidenc->width = GST_VIDEO_INFO_WIDTH (&state->info);
  cevidenc->height = GST_VIDEO_INFO_HEIGHT (&state->info);
  for (i = 0; i < GST_VIDEO_INFO_N_COMPONENTS (&state->info); i++)
    bpp += GST_VIDEO_INFO_COMP_DEPTH (&state->info, i);
  cevidenc->bpp = bpp;

  cevidenc->fps_num = GST_VIDEO_INFO_FPS_N (&state->info);
  cevidenc->fps_den = GST_VIDEO_INFO_FPS_D (&state->info);

  cevidenc->par_num = GST_VIDEO_INFO_PAR_N (&state->info);
  cevidenc->par_den = GST_VIDEO_INFO_PAR_D (&state->info);

  cevidenc->pix_format = GST_VIDEO_INFO_FORMAT (&state->info);

  /*$ 
   * TODO: 
   * 1. Average Duration needed??
   * 2. Configure Codec
   */

  /* some codecs support more than one format, first auto-choose one */
  GST_DEBUG_OBJECT (cevidenc, "picking an output format ...");
  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  if (!allowed_caps) {
    GST_DEBUG_OBJECT (cevidenc, "... but no peer, using template caps");
    /* we need to copy because get_allowed_caps returns a ref, and
     * get_pad_template_caps doesn't */
    allowed_caps =
        gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  }
  GST_DEBUG_OBJECT (cevidenc, "chose caps %" GST_PTR_FORMAT, allowed_caps);

  if (gst_caps_get_size (allowed_caps) > 1) {
    GstCaps *newcaps;

    newcaps = gst_caps_copy_nth (allowed_caps, 0);
    gst_caps_unref (allowed_caps);
    allowed_caps = newcaps;
  }

  /* Set output state */
  output_format =
      gst_video_encoder_set_output_state (encoder, allowed_caps, state);
  gst_video_codec_state_unref (output_format);

  return TRUE;

}


static gboolean
gst_cevidenc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
  return TRUE;
}

static GstFlowReturn
gst_cevidenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstCEVidEnc *cevidenc = (GstCEVidEnc *) encoder;
  gint ret_size = 0;

  /*$ 
   * TODO:
   * Add real code, 
   * This is just a temporal code for testing purposes 
   */
  gst_video_codec_frame_unref (frame);
  /* Get oldest frame */
  frame = gst_video_encoder_get_oldest_frame (encoder);

  ret_size = cevidenc->width * cevidenc->height;
  /* Allocate output buffer */
  if (gst_video_encoder_allocate_output_frame (encoder, frame,
          ret_size) != GST_FLOW_OK) {
    gst_video_codec_frame_unref (frame);
    goto alloc_fail;
  }

  return gst_video_encoder_finish_frame (encoder, frame);

alloc_fail:
  {
    GST_ERROR_OBJECT (cevidenc, "Failed to allocate buffer");

    return GST_FLOW_ERROR;
  }
}

static void
gst_cevidenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstCEVidEnc *cevidenc;
  GstCEVidEncClass *klass;

  /* Get a pointer of the right type. */
  cevidenc = (GstCEVidEnc *) (object);
  klass = (GstCEVidEncClass *) G_OBJECT_GET_CLASS (cevidenc);

  /* Check the argument id to see which argument we're setting. */
  switch (prop_id) {
    case PROP_OUTPUT_BUF_SIZE:
      cevidenc->out_buffer_size = g_value_get_int (value);
      GST_LOG ("setting \"outBufSize\" to %d", cevidenc->out_buffer_size);
      break;
    case PROP_COPY_OUTPUT:
      cevidenc->copy_output = g_value_get_boolean (value);
      GST_LOG ("seeting \"copyOutput\" to %s",
          cevidenc->copy_output ? "TRUE" : "FALSE");
      break;
    default:
      if (klass->codec->set_property)
        klass->codec->set_property (object, prop_id, value, pspec);
      else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* The set function is simply the inverse of the get fuction. */
static void
gst_cevidenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstCEVidEnc *cevidenc;
  GstCEVidEncClass *klass;

  /* It's not null if we got it, but it might not be ours */
  cevidenc = (GstCEVidEnc *) (object);
  klass = (GstCEVidEncClass *) G_OBJECT_GET_CLASS (cevidenc);

  switch (prop_id) {
    case PROP_OUTPUT_BUF_SIZE:
      g_value_set_int (value, cevidenc->out_buffer_size);
      break;
    case PROP_COPY_OUTPUT:
      g_value_set_boolean (value, cevidenc->copy_output);
      break;
    default:
      if (klass->codec->get_property)
        klass->codec->set_property (object, prop_id, value, pspec);
      else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_cevidenc_open (GstVideoEncoder * encoder)
{
  GstCEVidEnc *cevidenc = (GstCEVidEnc *) encoder;
  
  GST_DEBUG("opening %s Engine", CODEC_ENGINE);
  /* reset, load, and start DSP Engine */
  if ((cevidenc->ce_handle = Engine_open(CODEC_ENGINE, NULL, NULL)) == NULL) {
    GST_ELEMENT_ERROR(cevidenc,STREAM,CODEC_NOT_FOUND,(NULL),
        ("failed to open codec engine \"%s\"", CODEC_ENGINE));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_cevidenc_close (GstVideoEncoder * encoder)
{
  GstCEVidEnc *cevidenc = (GstCEVidEnc *) encoder;
  
  if (cevidenc->ce_handle) {
    GST_DEBUG("closing codec engine %p\n", cevidenc->ce_handle);
    Engine_close(cevidenc->ce_handle);
    cevidenc->ce_handle = NULL;
  }
  
  return TRUE;
}

static gboolean
gst_cevidenc_start (GstVideoEncoder * encoder)
{
  //~ GstCEVidEnc *cevidenc = (GstCEVidEnc *) encoder;

  return TRUE;
}

static gboolean
gst_cevidenc_stop (GstVideoEncoder * encoder)
{
  //~ GstCEVidEnc *cevidenc = (GstCEVidEnc *) encoder;

  return TRUE;
}

static GstFlowReturn
gst_cevidenc_finish (GstVideoEncoder * encoder)
{
  //~ GstCEVidEnc *cevidenc = (GstCEVidEnc *) encoder;

  return GST_FLOW_OK;
}

gboolean
gst_cevidenc_register (GstPlugin * plugin, GstCECodecData * codec)
{
  GTypeInfo typeinfo = {
    sizeof (GstCEVidEncClass),
    (GBaseInitFunc) gst_cevidenc_base_init,
    NULL,
    (GClassInitFunc) gst_cevidenc_class_init,
    NULL,
    NULL,
    sizeof (GstCEVidEnc),
    0,
    (GInstanceInitFunc) gst_cevidenc_init,
  };
  GType type;
  gchar *type_name;

  GST_LOG ("Registering encoder %s [%s]", codec->name, codec->long_name);

  /* construct the type */
  type_name = g_strdup_printf ("ce_%s", codec->name);
  type = g_type_from_name (type_name);

  if (!type) {
    /* create the glib type now */
    type =
        g_type_register_static (GST_TYPE_VIDEO_ENCODER, type_name, &typeinfo,
        0);
    g_type_set_qdata (type, GST_CEENC_PARAMS_QDATA, (gpointer) codec);

    {
      static const GInterfaceInfo preset_info = {
        NULL,
        NULL,
        NULL
      };
      g_type_add_interface_static (type, GST_TYPE_PRESET, &preset_info);
    }
  }

  if (!gst_element_register (plugin, type_name, GST_RANK_SECONDARY, type)) {
    g_free (type_name);
    return FALSE;
  }

  g_free (type_name);


  GST_LOG ("Finished registering encoder");

  return TRUE;
}
