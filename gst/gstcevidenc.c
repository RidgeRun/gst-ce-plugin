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

#include <ti/sdo/ce/osal/Memory.h>

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_RATECONTROL,
  PROP_ENCODINGPRESET,
  PROP_MAXBITRATE,
  PROP_TARGETBITRATE,
  PROP_INTRAFRAMEINTERVAL,
  PROP_FORCE_FRAME,
  PROP_CODEC_BASE
};

#define PROP_ENCODINGPRESET_DEFAULT       XDM_HIGH_SPEED
#define PROP_MAXBITRATE_DEFAULT           6000000
#define PROP_TARGETBITRATE_DEFAULT        6000000
#define PROP_INTRAFRAMEINTERVAL_DEFAULT   30
#define PROP_FORCE_FRAME_DEFAULT          IVIDEO_NA_FRAME

#define GST_CE_VIDENC_RATE_TYPE (gst_cevidenc_rate_get_type())
static GType
gst_cevidenc_rate_get_type (void)
{
  static GType rate_type = 0;

  static const GEnumValue rate_types[] = {
    {IVIDEO_LOW_DELAY, "Constant Bit Rate, for video conferencing", "CBR"},
    {IVIDEO_STORAGE, "Variable Bit Rate, for storage", "VBR"},
    {IVIDEO_TWOPASS, "Two pass rate, for non real-time applications",
        "Two Pass"},
    {IVIDEO_NONE, "No Rate Control is used", "None"},
    {IVIDEO_USER_DEFINED, "User defined on extended parameters", "User"},
    {0, NULL, NULL}
  };

  if (!rate_type) {
    rate_type = g_enum_register_static ("GstCEVidEncRate", rate_types);
  }
  return rate_type;
}

#define GST_CE_VIDENC_PRESET_TYPE (gst_cevidenc_preset_get_type())
static GType
gst_cevidenc_preset_get_type (void)
{
  static GType preset_type = 0;

  static const GEnumValue preset_types[] = {
    {XDM_HIGH_QUALITY, "High quality", "quality"},
    {XDM_HIGH_SPEED, "High speed, for storage", "speed"},
    {XDM_USER_DEFINED, "User defined on extended parameters", "user"},
    {0, NULL, NULL}
  };

  if (!preset_type) {
    preset_type = g_enum_register_static ("GstCEVidEncPreset", preset_types);
  }
  return preset_type;
}

#define GST_CE_VIDENC_FORCE_FRAME_TYPE (gst_cevidenc_force_frame_get_type())
static GType
gst_cevidenc_force_frame_get_type (void)
{
  static GType force_frame_type = 0;

  static const GEnumValue force_frame_types[] = {
    {IVIDEO_NA_FRAME, "No forcing of any specific frame type for the frame",
        "na"},
    {IVIDEO_I_FRAME, "Force the frame to be encoded as I frame", "i-frame"},
    {IVIDEO_IDR_FRAME, "Force the frame to be encoded as an IDR frame",
        "idr-frame"},
    {0, NULL, NULL}
  };

  if (!force_frame_type) {
    force_frame_type =
        g_enum_register_static ("GstCEVidEncForce", force_frame_types);
  }
  return force_frame_type;
}

/* A number of function prototypes are given so we can refer to them later. */
static void gst_cevidenc_class_init (GstCEVidEncClass * klass);
static void gst_cevidenc_base_init (GstCEVidEncClass * klass);
static void gst_cevidenc_init (GstCEVidEnc * cevidenc);
static void gst_cevidenc_finalize (GObject * object);

static gboolean gst_cevidenc_open (GstVideoEncoder * encoder);
static gboolean gst_cevidenc_close (GstVideoEncoder * encoder);
static gboolean gst_cevidenc_stop (GstVideoEncoder * encoder);
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
static gboolean gst_cevidenc_reset (GstVideoEncoder * encoder);

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

  g_object_class_install_property (gobject_class, PROP_RATECONTROL,
      g_param_spec_enum ("rate-control", "Encoding rate control",
          "Encoding rate control", GST_CE_VIDENC_RATE_TYPE,
          IVIDEO_RATECONTROLPRESET_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ENCODINGPRESET,
      g_param_spec_enum ("encoding-preset", "Encoding preset",
          "Encoding preset", GST_CE_VIDENC_PRESET_TYPE,
          PROP_ENCODINGPRESET_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAXBITRATE,
      g_param_spec_int ("max-bitrate",
          "Maximum bit rate",
          "Maximum bit-rate to be supported in bits per second",
          1000, 50000000, PROP_MAXBITRATE_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_TARGETBITRATE,
      g_param_spec_int ("target-bitrate",
          "Target bit rate",
          "Target bit-rate in bits per second, should be <= than the maxbitrate",
          1000, 20000000, PROP_TARGETBITRATE_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_INTRAFRAMEINTERVAL,
      g_param_spec_int ("intraframe-interval",
          "Intra frame interval",
          "Interval between two consecutive intra frames",
          0, G_MAXINT32, PROP_INTRAFRAMEINTERVAL_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_FORCE_FRAME,
      g_param_spec_enum ("force-frame",
          "Force frame",
          "Force next frame to be encoded as a specific type",
          GST_CE_VIDENC_FORCE_FRAME_TYPE, PROP_FORCE_FRAME_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Register additional properties, dependent on the exact CODEC */
  if (klass->codec && klass->codec->install_properties) {
    klass->codec->install_properties (gobject_class, PROP_CODEC_BASE);
  }

  venc_class->open = gst_cevidenc_open;
  venc_class->close = gst_cevidenc_close;
  venc_class->stop = gst_cevidenc_stop;
  venc_class->handle_frame = gst_cevidenc_handle_frame;
  venc_class->set_format = gst_cevidenc_set_format;
  venc_class->propose_allocation = gst_cevidenc_propose_allocation;

  gobject_class->finalize = gst_cevidenc_finalize;
}

static void
gst_cevidenc_init (GstCEVidEnc * cevidenc)
{

  GST_DEBUG_OBJECT (cevidenc, "initialize encoder");

  /* Allocate the codec params */
  if (!cevidenc->codec_params) {
    GST_DEBUG_OBJECT (cevidenc, "allocating codec params");
    cevidenc->codec_params = g_malloc0 (sizeof (VIDENC1_Params));
    if (!cevidenc->codec_params) {
      GST_WARNING_OBJECT (cevidenc, "failed to allocate VIDENC1_Params");
      return;
    }
    cevidenc->codec_params->size = sizeof (VIDENC1_Params);
  }

  if (!cevidenc->codec_dyn_params) {
    GST_DEBUG_OBJECT (cevidenc, "allocating codec dynamic params");
    cevidenc->codec_dyn_params = g_malloc0 (sizeof (VIDENC1_DynamicParams));
    if (!cevidenc->codec_dyn_params) {
      GST_WARNING_OBJECT (cevidenc, "failed to allocate VIDENC1_DynamicParams");
      return;
    }
    cevidenc->codec_dyn_params->size = sizeof (VIDENC1_DynamicParams);
  }

  cevidenc->first_buffer = TRUE;
  cevidenc->engine_handle = NULL;
  cevidenc->allocator = NULL;
  cevidenc->codec_private = NULL;

  gst_cevidenc_reset ((GstVideoEncoder *) cevidenc);
}

static void
gst_cevidenc_finalize (GObject * object)
{
  GstCEVidEnc *cevidenc = (GstCEVidEnc *) (object);

  /* Allocate the codec params */
  if (cevidenc->codec_params) {
    g_free (cevidenc->codec_params);
    cevidenc->codec_params = NULL;
  }

  if (cevidenc->codec_dyn_params) {
    g_free (cevidenc->codec_dyn_params);
    cevidenc->codec_dyn_params = NULL;
  }

  if (cevidenc->codec_private) {
    g_free (cevidenc->codec_private);
    cevidenc->codec_private = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_cevidenc_configure_codec:
 * Based on the negotiated format, create and initialize the 
 * codec instance*/
static gboolean
gst_cevidenc_configure_codec (GstCEVidEnc * cevidenc)
{
  GstCEVidEncClass *klass;
  VIDENC1_Status enc_status;
  VIDENC1_Params *params;
  VIDENC1_DynamicParams *dyn_params;
  gint fps;
  gint ret;
  gint i;

  klass = (GstCEVidEncClass *) G_OBJECT_GET_CLASS (cevidenc);
  params = cevidenc->codec_params;
  dyn_params = cevidenc->codec_dyn_params;

  g_return_val_if_fail (params, FALSE);
  g_return_val_if_fail (dyn_params, FALSE);

  /* Set the caps on the parameters of the encoder */
  switch (cevidenc->video_format) {
    case GST_VIDEO_FORMAT_UYVY:
      params->inputChromaFormat = XDM_YUV_422ILE;
      dyn_params->captureWidth = cevidenc->inbuf_desc.framePitch / 2;
      break;
    case GST_VIDEO_FORMAT_NV12:
      params->inputChromaFormat = XDM_YUV_420SP;
      dyn_params->captureWidth = cevidenc->inbuf_desc.framePitch;
      break;
    default:
      GST_ELEMENT_ERROR (cevidenc, STREAM, NOT_IMPLEMENTED,
          ("unsupported format in video stream: %d\n",
              cevidenc->video_format), (NULL));
      return FALSE;
  }

  fps = (cevidenc->fps_num * 1000) / cevidenc->fps_den;

  GST_OBJECT_LOCK (cevidenc);

  params->maxWidth = cevidenc->inbuf_desc.frameWidth;
  params->maxHeight = cevidenc->inbuf_desc.frameHeight;
  params->maxFrameRate = fps;

  dyn_params->inputWidth = cevidenc->inbuf_desc.frameWidth;
  dyn_params->inputHeight = cevidenc->inbuf_desc.frameHeight;
  dyn_params->refFrameRate = dyn_params->targetFrameRate = fps;

  if (cevidenc->codec_handle) {
    GST_DEBUG_OBJECT (cevidenc, "Closing old codec session");
    VIDENC1_delete (cevidenc->codec_handle);
  }

  GST_DEBUG_OBJECT (cevidenc, "Create the codec handle");
  cevidenc->codec_handle = VIDENC1_create (cevidenc->engine_handle,
      (Char *) klass->codec->name, params);
  if (!cevidenc->codec_handle)
    goto fail_open_codec;

  enc_status.size = sizeof (VIDENC1_Status);
  enc_status.data.buf = NULL;

  GST_DEBUG_OBJECT (cevidenc, "Set codec dynamic parameters");
  ret = VIDENC1_control (cevidenc->codec_handle, XDM_SETPARAMS,
      dyn_params, &enc_status);
  if (ret != VIDENC1_EOK)
    goto fail_control_params;

  GST_OBJECT_UNLOCK (cevidenc);

  /* Get buffer information from video encoder */
  ret = VIDENC1_control (cevidenc->codec_handle, XDM_GETBUFINFO,
      dyn_params, &enc_status);
  if (ret != VIDENC1_EOK)
    goto fail_control_getinfo;

  for (i = 0; i < enc_status.bufInfo.minNumInBufs; i++) {
    cevidenc->inbuf_desc.bufDesc[i].bufSize =
        enc_status.bufInfo.minInBufSize[i];
    GST_DEBUG_OBJECT (cevidenc, "size of input buffer [%d] = %li", i,
        cevidenc->inbuf_desc.bufDesc[i].bufSize);
  }

  cevidenc->outbuf_size = enc_status.bufInfo.minOutBufSize[0];
  cevidenc->outbuf_desc.numBufs = 1;
  cevidenc->outbuf_desc.bufSizes = (XDAS_Int32 *) & cevidenc->outbuf_size;
  GST_DEBUG_OBJECT (cevidenc, "output buffer size = %d", cevidenc->outbuf_size);

  return TRUE;

fail_open_codec:
  {
    GST_ERROR_OBJECT (cevidenc, "failed to open codec %s", klass->codec->name);
    GST_OBJECT_UNLOCK (cevidenc);
    return FALSE;
  }
fail_control_params:
  {
    GST_ERROR_OBJECT (cevidenc, "failed to set dynamic parameters, "
        "status error %x, %d", (guint) enc_status.extendedError, ret);
    GST_OBJECT_UNLOCK (cevidenc);
    VIDENC1_delete (cevidenc->codec_handle);
    cevidenc->codec_handle = NULL;
    return FALSE;
  }
fail_control_getinfo:
  {
    GST_ERROR_OBJECT (cevidenc, "failed to get buffer information, "
        "status error %x, %d", (guint) enc_status.extendedError, ret);
    VIDENC1_delete (cevidenc->codec_handle);
    cevidenc->codec_handle = NULL;
    return FALSE;
  }
}

static gboolean
gst_cevidenc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  GstCaps *allowed_caps;
  GstVideoCodecState *output_format;
  GstBuffer *codec_data = NULL;
  gint i, bpp = 0;

  GstCEVidEnc *cevidenc = (GstCEVidEnc *) encoder;
  GstCEVidEncClass *klass = (GstCEVidEncClass *) G_OBJECT_GET_CLASS (cevidenc);

  GST_DEBUG_OBJECT (cevidenc, "extracting common video information");

  /* Prepare the input buffer descriptor */
  cevidenc->inbuf_desc.frameWidth = GST_VIDEO_INFO_WIDTH (&state->info);
  cevidenc->inbuf_desc.frameHeight = GST_VIDEO_INFO_HEIGHT (&state->info);
  cevidenc->inbuf_desc.framePitch =
      GST_VIDEO_INFO_PLANE_STRIDE (&state->info, 0);
  cevidenc->inbuf_desc.numBufs = GST_VIDEO_INFO_N_PLANES (&state->info);

  for (i = 0; i < GST_VIDEO_INFO_N_COMPONENTS (&state->info); i++)
    bpp += GST_VIDEO_INFO_COMP_DEPTH (&state->info, i);

  cevidenc->bpp = bpp;

  cevidenc->fps_num = GST_VIDEO_INFO_FPS_N (&state->info);
  cevidenc->fps_den = GST_VIDEO_INFO_FPS_D (&state->info);

  cevidenc->par_num = GST_VIDEO_INFO_PAR_N (&state->info);
  cevidenc->par_den = GST_VIDEO_INFO_PAR_D (&state->info);

  cevidenc->video_format = GST_VIDEO_INFO_FORMAT (&state->info);

  GST_DEBUG_OBJECT (cevidenc, "input buffer format: width=%d, height=%d,"
      " pitch=%d, bpp=%d", cevidenc->inbuf_desc.frameWidth,
      cevidenc->inbuf_desc.frameHeight, cevidenc->inbuf_desc.framePitch,
      cevidenc->bpp);

  if (!gst_cevidenc_configure_codec (cevidenc))
    goto fail_set_caps;

  /* some codecs support more than one format, first auto-choose one */
  GST_DEBUG_OBJECT (cevidenc, "choosing an output format...");
  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  if (!allowed_caps) {
    GST_DEBUG_OBJECT (cevidenc, "... but no peer, using template caps");
    /* we need to copy because get_allowed_caps returns a ref, and
     * get_pad_template_caps doesn't */
    allowed_caps =
        gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  }
  GST_DEBUG_OBJECT (cevidenc, "chose caps %" GST_PTR_FORMAT, allowed_caps);

  if (klass->codec && klass->codec->set_src_caps) {
    GST_DEBUG ("use custom set src caps");
    if (!klass->codec->set_src_caps ((GObject *) cevidenc, &allowed_caps,
            &codec_data))
      goto fail_set_caps;
  }

  if (gst_caps_get_size (allowed_caps) > 1) {
    GstCaps *newcaps;

    newcaps = gst_caps_copy_nth (allowed_caps, 0);
    gst_caps_unref (allowed_caps);
    allowed_caps = newcaps;
  }

  /* Store input state and set output state */
  if (cevidenc->input_state)
    gst_video_codec_state_unref (cevidenc->input_state);
  cevidenc->input_state = gst_video_codec_state_ref (state);

  /* Set output state */
  output_format =
      gst_video_encoder_set_output_state (encoder, allowed_caps, state);

  if (!output_format) {
    goto fail_set_caps;
  }

  if (codec_data) {
    GST_DEBUG_OBJECT (cevidenc, "setting the codec data");
    output_format->codec_data = codec_data;
  }
  gst_video_codec_state_unref (output_format);

  return TRUE;

fail_set_caps:
  GST_ERROR_OBJECT (cevidenc, "couldn't set video format");
  return FALSE;
}

static gboolean
gst_cevidenc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  GstCEVidEnc *cevidenc = (GstCEVidEnc *) encoder;
  GstAllocationParams params = { 0, 3, 0, 0 };

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  gst_query_add_allocation_param (query, cevidenc->allocator, &params);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
}

/**
 * gst_cevidenc_allocate_output_frame
 * 
 * Allocates a CMEM output buffer 
 */
static gboolean
gst_cevidenc_allocate_output_frame (GstCEVidEnc * cevidenc, GstBuffer ** buf)
{

  /*Get allocator parameters */
  gst_video_encoder_get_allocator ((GstVideoEncoder *) cevidenc, NULL,
      &cevidenc->alloc_params);
  cevidenc->alloc_params.align = 3;

  *buf = gst_buffer_new_allocate (cevidenc->allocator, cevidenc->outbuf_size,
      &cevidenc->alloc_params);

  if (!*buf) {
    GST_DEBUG_OBJECT (cevidenc, "Can't alloc output buffer");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_cevidenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstCEVidEnc *cevidenc = (GstCEVidEnc *) encoder;
  GstCEVidEncClass *klass = (GstCEVidEncClass *) G_OBJECT_GET_CLASS (cevidenc);
  GstVideoInfo *info = &cevidenc->input_state->info;
  GstVideoFrame vframe;
  GstMapInfo info_out;
  GstBuffer *outbuf = NULL;
  gint ret = 0;
  gint c;
  GstCEMeta *meta;
  VIDENC1_InArgs in_args;
  VIDENC1_OutArgs out_args;

  /* Fill planes pointer */
  if (!gst_video_frame_map (&vframe, info, frame->input_buffer, GST_MAP_READ))
    goto fail_map;

  for (c = 0; c < GST_VIDEO_FRAME_N_PLANES (&vframe); c++) {
    cevidenc->inbuf_desc.bufDesc[c].buf =
        GST_VIDEO_FRAME_PLANE_DATA (&vframe, c);
  }
  /* $
   * TODO
   * No CMEM contiguous buffers should be registered to Codec Engine
   * How should we manage the buffer registration?
   */

  gst_video_frame_unmap (&vframe);

  /* If no CE meta try to register the buffer to Codec Engine
   * adding the CE meta*/
  meta = GST_CE_META_GET (frame->input_buffer);
  if (!meta) {
    meta = GST_CE_META_ADD (frame->input_buffer);
    if (meta) {
      /* Indicates that the metadata is managed by the 
       * buffer pool and shouldn't be removed*/
      GST_META_FLAG_SET (meta, GST_META_FLAG_POOLED);
      GST_META_FLAG_SET (meta, GST_META_FLAG_LOCKED);
    } else {
      /* $
       * TODO
       * TODO
       * Failing if input buffer is not contiguous. Should copy the buffer
       * instead of fail?
       */
      goto fail_no_contiguous_buffer;
    }
  }

  /* Allocate output buffer */
  if (!gst_cevidenc_allocate_output_frame (cevidenc, &outbuf))
    goto fail_alloc;

  if (!gst_buffer_map (outbuf, &info_out, GST_MAP_WRITE)) {
    goto fail_map;
  }

  cevidenc->outbuf_desc.bufs = (XDAS_Int8 **) & (info_out.data);

  /* Set output and input arguments for the encode process */
  in_args.size = sizeof (IVIDENC1_InArgs);
  in_args.inputID = 1;
  in_args.topFieldFirstFlag = 1;

  out_args.size = sizeof (VIDENC1_OutArgs);

  /* Encode process */
  ret =
      VIDENC1_process (cevidenc->codec_handle, &cevidenc->inbuf_desc,
      &cevidenc->outbuf_desc, &in_args, &out_args);
  if (ret != VIDENC1_EOK)
    goto fail_encode;

  GST_DEBUG_OBJECT (cevidenc, "encoded an output buffer of size %li %p",
      out_args.bytesGenerated, *cevidenc->outbuf_desc.bufs);

  gst_buffer_unmap (outbuf, &info_out);

  gst_buffer_set_size (outbuf, out_args.bytesGenerated);
  if (klass->codec->post_process)
    klass->codec->post_process ((GObject *) cevidenc, outbuf);

  /*Mark when finish to process the first buffer */
  if (cevidenc->first_buffer)
    cevidenc->first_buffer = FALSE;

  GST_DEBUG_OBJECT (cevidenc, "frame encoded succesfully");
  gst_video_codec_frame_unref (frame);
  /* Get oldest frame */
  frame = gst_video_encoder_get_oldest_frame (encoder);
  frame->output_buffer = outbuf;

  /* Mark I and IDR frames */
  if ((out_args.encodedFrameType == IVIDEO_I_FRAME) ||
      (out_args.encodedFrameType == IVIDEO_IDR_FRAME)) {
    GST_DEBUG_OBJECT (cevidenc, "frame type %d", out_args.encodedFrameType);
    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
  }

  return gst_video_encoder_finish_frame (encoder, frame);

fail_map:
  {
    GST_ERROR_OBJECT (encoder, "Failed to map input buffer");
    return GST_FLOW_ERROR;
  }
fail_no_contiguous_buffer:
  {
    GST_ERROR_OBJECT (encoder, "Input buffer should be contiguous");
    return GST_FLOW_ERROR;
  }
fail_alloc:
  {
    GST_ERROR_OBJECT (cevidenc, "Failed to get output buffer");
    return GST_FLOW_ERROR;
  }
fail_encode:
  {
    gst_buffer_unmap (outbuf, &info_out);
    GST_ERROR_OBJECT (cevidenc,
        "Failed encode process with extended error: 0x%x",
        (unsigned int) out_args.extendedError);
    return GST_FLOW_ERROR;
  }
}

static void
gst_cevidenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstCEVidEnc *cevidenc;
  GstCEVidEncClass *klass;
  VIDENC1_Params *params;
  VIDENC1_DynamicParams *dyn_params;
  VIDENC1_Status enc_status;
  gboolean set_params = FALSE;

  gint ret;

  /* Get a pointer of the right type. */
  cevidenc = (GstCEVidEnc *) (object);
  klass = (GstCEVidEncClass *) G_OBJECT_GET_CLASS (cevidenc);

  if ((!cevidenc->codec_params) || (!cevidenc->codec_dyn_params)) {
    GST_WARNING_OBJECT (cevidenc, "couldn't set property");
    return;
  }

  params = (VIDENC1_Params *) cevidenc->codec_params;
  dyn_params = (VIDENC1_DynamicParams *) cevidenc->codec_dyn_params;

  GST_OBJECT_LOCK (cevidenc);
  /* Check the argument id to see which argument we're setting. */
  switch (prop_id) {
    case PROP_RATECONTROL:
      params->rateControlPreset = g_value_get_enum (value);
      GST_LOG_OBJECT (cevidenc,
          "setting encoding rate control to %li", params->rateControlPreset);
      break;
    case PROP_ENCODINGPRESET:
      params->encodingPreset = g_value_get_enum (value);
      GST_LOG_OBJECT (cevidenc,
          "setting encoding rate control to %li", params->encodingPreset);
      break;
    case PROP_MAXBITRATE:
      params->maxBitRate = g_value_get_int (value);
      GST_LOG_OBJECT (cevidenc,
          "setting max bitrate to %li", params->maxBitRate);
      break;
    case PROP_TARGETBITRATE:
      dyn_params->targetBitRate = g_value_get_int (value);
      GST_LOG_OBJECT (cevidenc,
          "setting target bitrate to %li", dyn_params->targetBitRate);
      set_params = TRUE;
      break;
    case PROP_INTRAFRAMEINTERVAL:
      dyn_params->intraFrameInterval = g_value_get_int (value);
      GST_LOG_OBJECT (cevidenc,
          "setting intra frame interval to %li",
          dyn_params->intraFrameInterval);
      set_params = TRUE;
      break;
    case PROP_FORCE_FRAME:
      dyn_params->forceFrame = g_value_get_int (value);
      GST_LOG_OBJECT (cevidenc, "forcing frame to %li", dyn_params->forceFrame);
      set_params = TRUE;
      break;
    default:
      if (klass->codec->set_property)
        klass->codec->set_property (object, prop_id, value, pspec,
            PROP_CODEC_BASE);
      else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  if (set_params && cevidenc->codec_handle) {
    enc_status.size = sizeof (VIDENC1_Status);
    enc_status.data.buf = NULL;
    ret = VIDENC1_control (cevidenc->codec_handle, XDM_SETPARAMS,
        dyn_params, &enc_status);
    if (ret != VIDENC1_EOK)
      GST_WARNING_OBJECT (cevidenc, "failed to set dynamic parameters, "
          "status error %x, %d", (guint) enc_status.extendedError, ret);
  }

  GST_OBJECT_UNLOCK (cevidenc);
}

/* The set function is simply the inverse of the get fuction. */
static void
gst_cevidenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstCEVidEnc *cevidenc;
  GstCEVidEncClass *klass;
  VIDENC1_Params *params;
  VIDENC1_DynamicParams *dyn_params;

  /* It's not null if we got it, but it might not be ours */
  cevidenc = (GstCEVidEnc *) (object);
  klass = (GstCEVidEncClass *) G_OBJECT_GET_CLASS (cevidenc);

  if ((!cevidenc->codec_params) || (!cevidenc->codec_dyn_params)) {
    GST_WARNING_OBJECT (cevidenc, "couldn't set property");
    return;
  }

  params = (VIDENC1_Params *) cevidenc->codec_params;
  dyn_params = (VIDENC1_DynamicParams *) cevidenc->codec_dyn_params;

  GST_OBJECT_LOCK (cevidenc);
  switch (prop_id) {
    case PROP_RATECONTROL:
      g_value_set_enum (value, params->rateControlPreset);
      break;
    case PROP_ENCODINGPRESET:
      g_value_set_enum (value, params->encodingPreset);
      break;
    case PROP_MAXBITRATE:
      g_value_set_int (value, params->maxBitRate);
      break;
    case PROP_TARGETBITRATE:
      g_value_set_int (value, dyn_params->targetBitRate);
      break;
    case PROP_INTRAFRAMEINTERVAL:
      g_value_set_int (value, dyn_params->intraFrameInterval);
      break;
    case PROP_FORCE_FRAME:
      g_value_set_enum (value, dyn_params->forceFrame);
      break;
    default:
      if (klass->codec->get_property)
        klass->codec->get_property (object, prop_id, value, pspec,
            PROP_CODEC_BASE);
      else
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (cevidenc);
}

static gboolean
gst_cevidenc_open (GstVideoEncoder * encoder)
{
  GstCEVidEnc *cevidenc = (GstCEVidEnc *) encoder;

  GST_DEBUG_OBJECT (cevidenc, "opening %s Engine", CODEC_ENGINE);
  /* reset, load, and start DSP Engine */
  if ((cevidenc->engine_handle =
          Engine_open ((Char *) CODEC_ENGINE, NULL, NULL)) == NULL)
    goto fail_engine_open;

  if (cevidenc->allocator)
    gst_object_unref (cevidenc->allocator);

  GST_DEBUG_OBJECT (cevidenc, "getting CMEM allocator");
  cevidenc->allocator = gst_allocator_find ("ContiguousMemory");

  if (!cevidenc->allocator)
    goto fail_no_allocator;

  return TRUE;

  /* Errors */
fail_engine_open:
  {
    GST_ELEMENT_ERROR (cevidenc, STREAM, CODEC_NOT_FOUND, (NULL),
        ("failed to open codec engine \"%s\"", CODEC_ENGINE));
    return FALSE;
  }
fail_no_allocator:
  {
    GST_WARNING_OBJECT (cevidenc, "can't find the CMEM allocator");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_cevidenc_close (GstVideoEncoder * encoder)
{
  GstCEVidEnc *cevidenc = (GstCEVidEnc *) encoder;

  if (cevidenc->engine_handle) {
    GST_DEBUG_OBJECT (cevidenc, "closing codec engine %p\n",
        cevidenc->engine_handle);
    Engine_close (cevidenc->engine_handle);
    cevidenc->engine_handle = NULL;
  }

  if (cevidenc->allocator) {
    gst_object_unref (cevidenc->allocator);
    cevidenc->allocator = NULL;
  }

  return TRUE;
}

static gboolean
gst_cevidenc_stop (GstVideoEncoder * encoder)
{
  return gst_cevidenc_reset (encoder);
}

static gboolean
gst_cevidenc_reset (GstVideoEncoder * encoder)
{
  GstCEVidEnc *cevidenc = (GstCEVidEnc *) encoder;
  GstCEVidEncClass *klass = (GstCEVidEncClass *) G_OBJECT_GET_CLASS (cevidenc);

  VIDENC1_Params *params = cevidenc->codec_params;
  VIDENC1_DynamicParams *dyn_params = cevidenc->codec_dyn_params;

  g_return_val_if_fail (params, FALSE);
  g_return_val_if_fail (dyn_params, FALSE);

  if (cevidenc->input_state) {
    gst_video_codec_state_unref (cevidenc->input_state);
    cevidenc->input_state = NULL;
  }

  if (cevidenc->codec_handle) {
    VIDENC1_delete (cevidenc->codec_handle);
    cevidenc->codec_handle = NULL;
  }

  GST_OBJECT_LOCK (cevidenc);

  /* Set default values for codec static params */
  params->encodingPreset = PROP_ENCODINGPRESET_DEFAULT;
  params->rateControlPreset = IVIDEO_RATECONTROLPRESET_DEFAULT;
  params->maxBitRate = PROP_MAXBITRATE_DEFAULT;
  params->dataEndianness = XDM_BYTE;
  params->maxInterFrameInterval = 1;
  params->inputChromaFormat = XDM_YUV_420P;
  params->inputContentType = IVIDEO_PROGRESSIVE;
  params->reconChromaFormat = XDM_CHROMA_NA;

  /* Set default values for codec dynamic params */
  dyn_params->targetBitRate = PROP_TARGETBITRATE_DEFAULT;
  dyn_params->intraFrameInterval = PROP_INTRAFRAMEINTERVAL_DEFAULT;
  dyn_params->generateHeader = XDM_ENCODE_AU;
  dyn_params->captureWidth = 0;
  dyn_params->forceFrame = PROP_FORCE_FRAME_DEFAULT;
  dyn_params->interFrameInterval = 1;
  dyn_params->mbDataFlag = 0;

  GST_OBJECT_UNLOCK (cevidenc);

  /* Configure specific codec */
  if (klass->codec && klass->codec->setup) {
    GST_DEBUG_OBJECT (cevidenc, "configuring codec");
    klass->codec->setup ((GObject *) cevidenc);
  }

  return TRUE;
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
