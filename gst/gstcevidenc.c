/*
 * gstcevidenc.c
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

/**
 * SECTION:gstcevidenc
 * @short_description: Base class for Codec Engine video encoders
 * @see_also:
 *
 * This base class is for video encoders turning raw video into
 * encoded video data using TI codecs with the VIDENC1 video encoding
 * interface.
 * 
 * Subclass is responsible for providing pad template caps for
 * source and sink pads. The pads need to be named "sink" and "src".
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

#define GST_CEVIDENC_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_CEVIDENC, GstCEVidEncPrivate))

struct _GstCEVidEncPrivate
{
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

  /* Codec Data */
  Engine_Handle engine_handle;
  IVIDEO1_BufDescIn inbuf_desc;
  XDM_BufDesc outbuf_desc;
};

/* A number of function prototypes are given so we can refer to them later. */
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



#define gst_cevidenc_parent_class parent_class
G_DEFINE_TYPE (GstCEVidEnc, gst_cevidenc, GST_TYPE_VIDEO_ENCODER);

static void
gst_cevidenc_class_init (GstCEVidEncClass * klass)
{
  GObjectClass *gobject_class;
  GstVideoEncoderClass *venc_class;

  gobject_class = (GObjectClass *) klass;
  venc_class = (GstVideoEncoderClass *) klass;

  g_type_class_add_private (klass, sizeof (GstCEVidEncPrivate));

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
  GstCEVidEncPrivate *priv;

  GST_DEBUG_OBJECT (cevidenc, "initialize encoder");

  priv = cevidenc->priv = GST_CEVIDENC_GET_PRIVATE (cevidenc);

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

  priv->first_buffer = TRUE;
  priv->engine_handle = NULL;
  priv->allocator = NULL;

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
  GstCEVidEncPrivate *priv;
  VIDENC1_Status enc_status;
  VIDENC1_Params *params;
  VIDENC1_DynamicParams *dyn_params;
  gint fps;
  gint ret;
  gint i;


  klass = (GstCEVidEncClass *) G_OBJECT_GET_CLASS (cevidenc);
  priv = cevidenc->priv;
  params = cevidenc->codec_params;
  dyn_params = cevidenc->codec_dyn_params;

  g_return_val_if_fail (params, FALSE);
  g_return_val_if_fail (dyn_params, FALSE);
  g_return_val_if_fail (klass->codec_name, FALSE);

  /* Set the caps on the parameters of the encoder */
  switch (priv->video_format) {
    case GST_VIDEO_FORMAT_UYVY:
      params->inputChromaFormat = XDM_YUV_422ILE;
      dyn_params->captureWidth = priv->inbuf_desc.framePitch / 2;
      break;
    case GST_VIDEO_FORMAT_NV12:
      params->inputChromaFormat = XDM_YUV_420SP;
      dyn_params->captureWidth = priv->inbuf_desc.framePitch;
      break;
    default:
      GST_ELEMENT_ERROR (cevidenc, STREAM, NOT_IMPLEMENTED,
          ("unsupported format in video stream: %d\n",
              priv->video_format), (NULL));
      return FALSE;
  }

  fps = (priv->fps_num * 1000) / priv->fps_den;

  GST_OBJECT_LOCK (cevidenc);

  params->maxWidth = priv->inbuf_desc.frameWidth;
  params->maxHeight = priv->inbuf_desc.frameHeight;
  params->maxFrameRate = fps;

  dyn_params->inputWidth = priv->inbuf_desc.frameWidth;
  dyn_params->inputHeight = priv->inbuf_desc.frameHeight;
  dyn_params->refFrameRate = dyn_params->targetFrameRate = fps;

  if (cevidenc->codec_handle) {
    GST_DEBUG_OBJECT (cevidenc, "Closing old codec session");
    VIDENC1_delete (cevidenc->codec_handle);
  }

  GST_DEBUG_OBJECT (cevidenc, "Create the codec handle");
  cevidenc->codec_handle = VIDENC1_create (priv->engine_handle,
      (Char *) klass->codec_name, params);
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
    priv->inbuf_desc.bufDesc[i].bufSize = enc_status.bufInfo.minInBufSize[i];
    GST_DEBUG_OBJECT (cevidenc, "size of input buffer [%d] = %li", i,
        priv->inbuf_desc.bufDesc[i].bufSize);
  }

  priv->outbuf_size = enc_status.bufInfo.minOutBufSize[0];
  priv->outbuf_desc.numBufs = 1;
  priv->outbuf_desc.bufSizes = (XDAS_Int32 *) & priv->outbuf_size;
  GST_DEBUG_OBJECT (cevidenc, "output buffer size = %d", priv->outbuf_size);

  return TRUE;

fail_open_codec:
  {
    GST_ERROR_OBJECT (cevidenc, "failed to open codec %s", klass->codec_name);
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
  GstCEVidEncPrivate *priv = cevidenc->priv;

  GST_DEBUG_OBJECT (cevidenc, "extracting common video information");

  /* Prepare the input buffer descriptor */
  priv->inbuf_desc.frameWidth = GST_VIDEO_INFO_WIDTH (&state->info);
  priv->inbuf_desc.frameHeight = GST_VIDEO_INFO_HEIGHT (&state->info);
  priv->inbuf_desc.framePitch = GST_VIDEO_INFO_PLANE_STRIDE (&state->info, 0);
  priv->inbuf_desc.numBufs = GST_VIDEO_INFO_N_PLANES (&state->info);

  for (i = 0; i < GST_VIDEO_INFO_N_COMPONENTS (&state->info); i++)
    bpp += GST_VIDEO_INFO_COMP_DEPTH (&state->info, i);

  priv->bpp = bpp;

  priv->fps_num = GST_VIDEO_INFO_FPS_N (&state->info);
  priv->fps_den = GST_VIDEO_INFO_FPS_D (&state->info);

  priv->par_num = GST_VIDEO_INFO_PAR_N (&state->info);
  priv->par_den = GST_VIDEO_INFO_PAR_D (&state->info);

  priv->video_format = GST_VIDEO_INFO_FORMAT (&state->info);

  GST_DEBUG_OBJECT (cevidenc, "input buffer format: width=%li, height=%li,"
      " pitch=%li, bpp=%d", priv->inbuf_desc.frameWidth,
      priv->inbuf_desc.frameHeight, priv->inbuf_desc.framePitch, priv->bpp);

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

  if (klass->set_src_caps) {
    GST_DEBUG ("use custom set src caps");
    if (!klass->set_src_caps (cevidenc, &allowed_caps, &codec_data))
      goto fail_set_caps;
  }

  if (gst_caps_get_size (allowed_caps) > 1) {
    GstCaps *newcaps;

    newcaps = gst_caps_copy_nth (allowed_caps, 0);
    gst_caps_unref (allowed_caps);
    allowed_caps = newcaps;
  }

  /* Store input state and set output state */
  if (priv->input_state)
    gst_video_codec_state_unref (priv->input_state);
  priv->input_state = gst_video_codec_state_ref (state);

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
  GstCEVidEncPrivate *priv = cevidenc->priv;
  GstAllocationParams params;

  params.flags = 0;
  params.prefix = 0;
  params.padding = 0;

  /*
   * GstAllocationParams have an alignment that is a bitmask
   * so that align + 1 equals the amount of bytes to align to.
   */
  params.align = 31;

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  gst_query_add_allocation_param (query, priv->allocator, &params);

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
  GstCEVidEncPrivate *priv = cevidenc->priv;

  /* Get allocator parameters */
  gst_video_encoder_get_allocator ((GstVideoEncoder *) cevidenc, NULL,
      &priv->alloc_params);

  *buf = gst_buffer_new_allocate (priv->allocator, priv->outbuf_size,
      &priv->alloc_params);

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
  GstCEVidEncPrivate *priv = cevidenc->priv;
  GstCEVidEncClass *klass = (GstCEVidEncClass *) G_OBJECT_GET_CLASS (cevidenc);
  GstVideoInfo *info = &priv->input_state->info;
  GstVideoFrame vframe;
  GstMapInfo info_out;
  GstBuffer *outbuf = NULL;
  GstCEContigBufMeta *meta;
  VIDENC1_InArgs in_args;
  VIDENC1_OutArgs out_args;
  gint ret = 0;
  gint i;

  /* Fill planes pointer */
  if (!gst_video_frame_map (&vframe, info, frame->input_buffer, GST_MAP_READ))
    goto fail_map;

  for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (&vframe); i++) {
    priv->inbuf_desc.bufDesc[i].buf = GST_VIDEO_FRAME_PLANE_DATA (&vframe, i);
  }

  gst_video_frame_unmap (&vframe);

  /* If there's no contiguous buffer metadata, it hasn't been
   * registered as contiguous, so we attempt to register it. If 
   * we can't, then this buffer is not contiguous and we fail. */
  meta = GST_CE_CONTIG_BUF_META_GET (frame->input_buffer);
  if (!meta) {
    meta = GST_CE_CONTIG_BUF_META_ADD (frame->input_buffer);
    if (meta) {
      /* The metadata is now managed by the buffer pool and shouldn't
       * be removed */
      GST_META_FLAG_SET (meta, GST_META_FLAG_POOLED);
      GST_META_FLAG_SET (meta, GST_META_FLAG_LOCKED);
    } else {
      /* $
       * TODO
       * Failing if input buffer is not contiguous. Should it copy the
       * buffer instead?
       */
      goto fail_no_contiguous_buffer;
    }
  }

  /* Allocate output buffer */
  if (!gst_cevidenc_allocate_output_frame (cevidenc, &outbuf))
    goto fail_alloc;

  if (!gst_buffer_map (outbuf, &info_out, GST_MAP_WRITE))
    goto fail_map;

  priv->outbuf_desc.bufs = (XDAS_Int8 **) & (info_out.data);

  /* Set output and input arguments for the encoding process */
  in_args.size = sizeof (IVIDENC1_InArgs);
  in_args.inputID = 1;
  in_args.topFieldFirstFlag = 1;

  out_args.size = sizeof (VIDENC1_OutArgs);

  /* Pre-encode process */
  if (klass->pre_process && !klass->pre_process (cevidenc, frame->input_buffer))
    goto fail_pre_encode;

  /* Encode process */
  ret =
      VIDENC1_process (cevidenc->codec_handle, &priv->inbuf_desc,
      &priv->outbuf_desc, &in_args, &out_args);
  if (ret != VIDENC1_EOK)
    goto fail_encode;

  GST_DEBUG_OBJECT (cevidenc, "encoded an output buffer of size %li at addr %p",
      out_args.bytesGenerated, *priv->outbuf_desc.bufs);

  gst_buffer_unmap (outbuf, &info_out);

  gst_buffer_set_size (outbuf, out_args.bytesGenerated);

  /* Post-encode process */
  if (klass->post_process && !klass->post_process (cevidenc, outbuf))
    goto fail_post_encode;

  if (priv->first_buffer)
    priv->first_buffer = FALSE;

  GST_DEBUG_OBJECT (cevidenc, "frame encoded succesfully");

  /* Get oldest frame */
  gst_video_codec_frame_unref (frame);
  frame = gst_video_encoder_get_oldest_frame (encoder);
  frame->output_buffer = outbuf;

  /* Mark I and IDR frames */
  if ((out_args.encodedFrameType == IVIDEO_I_FRAME) ||
      (out_args.encodedFrameType == IVIDEO_IDR_FRAME)) {
    GST_DEBUG_OBJECT (cevidenc, "frame type %li", out_args.encodedFrameType);
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
fail_pre_encode:
  {
    gst_buffer_unmap (outbuf, &info_out);
    GST_ERROR_OBJECT (cevidenc, "Failed pre-encode process");
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
fail_post_encode:
  {
    GST_ERROR_OBJECT (cevidenc, "Failed post-encode process");
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
      if (!cevidenc->codec_handle) {
        params->rateControlPreset = g_value_get_enum (value);
        GST_LOG_OBJECT (cevidenc,
            "setting encoding rate control to %li", params->rateControlPreset);
      } else {
        goto fail_static_prop;
      }
      break;
    case PROP_ENCODINGPRESET:
      if (!cevidenc->codec_handle) {
        params->encodingPreset = g_value_get_enum (value);
        GST_LOG_OBJECT (cevidenc,
            "setting encoding rate control to %li", params->encodingPreset);
      } else {
        goto fail_static_prop;
      }
      break;
    case PROP_MAXBITRATE:
      if (!cevidenc->codec_handle) {
        params->maxBitRate = g_value_get_int (value);
        GST_LOG_OBJECT (cevidenc,
            "setting max bitrate to %li", params->maxBitRate);
      } else {
        goto fail_static_prop;
      }
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
  return;

fail_static_prop:
  GST_WARNING_OBJECT (cevidenc, "can't set static property when "
      "the codec is already configured");
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
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (cevidenc);
}

static gboolean
gst_cevidenc_open (GstVideoEncoder * encoder)
{
  GstCEVidEnc *cevidenc = (GstCEVidEnc *) encoder;
  GstCEVidEncPrivate *priv = cevidenc->priv;

  GST_DEBUG_OBJECT (cevidenc, "opening %s Engine", CODEC_ENGINE);
  /* reset, load, and start DSP Engine */
  if ((priv->engine_handle =
          Engine_open ((Char *) CODEC_ENGINE, NULL, NULL)) == NULL)
    goto fail_engine_open;

  if (priv->allocator)
    gst_object_unref (priv->allocator);

  GST_DEBUG_OBJECT (cevidenc, "getting CMEM allocator");
  priv->allocator = gst_allocator_find ("ContiguousMemory");

  if (!priv->allocator)
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
  GstCEVidEncPrivate *priv = cevidenc->priv;

  if (priv->engine_handle) {
    GST_DEBUG_OBJECT (cevidenc, "closing codec engine %p\n",
        priv->engine_handle);
    Engine_close (priv->engine_handle);
    priv->engine_handle = NULL;
  }

  if (priv->allocator) {
    gst_object_unref (priv->allocator);
    priv->allocator = NULL;
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
  GstCEVidEncPrivate *priv = cevidenc->priv;
  GstCEVidEncClass *klass = (GstCEVidEncClass *) G_OBJECT_GET_CLASS (cevidenc);

  VIDENC1_Params *params = cevidenc->codec_params;
  VIDENC1_DynamicParams *dyn_params = cevidenc->codec_dyn_params;

  g_return_val_if_fail (params, FALSE);
  g_return_val_if_fail (dyn_params, FALSE);

  if (priv->input_state) {
    gst_video_codec_state_unref (priv->input_state);
    priv->input_state = NULL;
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
  if (klass->reset) {
    GST_DEBUG_OBJECT (cevidenc, "configuring codec");
    klass->reset (cevidenc);
  }

  return TRUE;
}

/**
 * gst_cevidenc_get_header:
 * @cevidenc: a #GstCEVidEnc
 * @buffer: (out) (transfer full): the #GstBuffer containing the 
 *        encoding header.
 * @header_size: (out): the bytes generated for the header.
 * 
 * Lets #GstCEVidEnc sub-classes to obtain the encoding header,
 * that can be used to calculate the corresponding codec data.
 *
 * Unref the @buffer after use it.
 */
gboolean
gst_cevidenc_get_header (GstCEVidEnc * cevidenc, GstBuffer ** buffer,
    gint * header_size)
{
  GstCEVidEncPrivate *priv = cevidenc->priv;
  VIDENC1_Status enc_status;
  VIDENC1_InArgs in_args;
  VIDENC1_OutArgs out_args;
  GstBuffer *header_buf;
  GstMapInfo info;
  gint ret;

  g_return_val_if_fail (GST_IS_CEVIDENC (cevidenc), FALSE);

  GST_OBJECT_LOCK (cevidenc);

  if ((!cevidenc->codec_handle) || (!cevidenc->codec_dyn_params))
    return FALSE;

  GST_DEBUG_OBJECT (cevidenc, "get H.264 header");

  enc_status.size = sizeof (VIDENC1_Status);
  enc_status.data.buf = NULL;

  cevidenc->codec_dyn_params->generateHeader = XDM_GENERATE_HEADER;
  ret = VIDENC1_control (cevidenc->codec_handle, XDM_SETPARAMS,
      cevidenc->codec_dyn_params, &enc_status);
  if (ret != VIDENC1_EOK)
    goto fail_control_params;

  /*Allocate an output buffer for the header */
  header_buf = gst_buffer_new_allocate (priv->allocator, 200,
      &priv->alloc_params);
  if (!gst_buffer_map (header_buf, &info, GST_MAP_WRITE))
    return FALSE;

  priv->outbuf_desc.bufs = (XDAS_Int8 **) & (info.data);

  /* Set output and input arguments for the encode process */
  in_args.size = sizeof (IVIDENC1_InArgs);
  in_args.inputID = 1;
  in_args.topFieldFirstFlag = 1;

  out_args.size = sizeof (VIDENC1_OutArgs);

  /* Generate the header */
  ret =
      VIDENC1_process (cevidenc->codec_handle, &priv->inbuf_desc,
      &priv->outbuf_desc, &in_args, &out_args);
  if (ret != VIDENC1_EOK)
    goto fail_encode;

  gst_buffer_unmap (header_buf, &info);

  cevidenc->codec_dyn_params->generateHeader = XDM_ENCODE_AU;
  ret = VIDENC1_control (cevidenc->codec_handle, XDM_SETPARAMS,
      cevidenc->codec_dyn_params, &enc_status);
  if (ret != VIDENC1_EOK)
    goto fail_control_params;

  GST_OBJECT_UNLOCK (cevidenc);

  *header_size = out_args.bytesGenerated;
  *buffer = header_buf;

  return TRUE;

fail_control_params:
  {
    GST_OBJECT_UNLOCK (cevidenc);
    GST_WARNING_OBJECT (cevidenc, "Failed to set dynamic parameters, "
        "status error %x, %d", (unsigned int) enc_status.extendedError, ret);
    return FALSE;
  }
fail_encode:
  {
    GST_OBJECT_UNLOCK (cevidenc);
    GST_WARNING_OBJECT (cevidenc,
        "Failed header encode process with extended error: 0x%x",
        (unsigned int) out_args.extendedError);
    return FALSE;
  }
}
