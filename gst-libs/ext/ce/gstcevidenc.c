/*
 * gstcevidenc.c
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
#include <ext/cmem/gstceslicepool.h>

#include "gstcevidenc.h"

#include <ti/sdo/ce/osal/Memory.h>

GST_DEBUG_CATEGORY_STATIC (gst_ce_videnc_debug);
#define GST_CAT_DEFAULT gst_ce_videnc_debug

enum
{
  PROP_0,
  PROP_RATE_CONTROL,
  PROP_ENCODING_PRESET,
  PROP_MAX_BITRATE,
  PROP_TARGET_BITRATE,
  PROP_INTRA_FRAME_INTERVAL,
  PROP_FORCE_FRAME,
  PROP_NUM_OUT_BUFFERS,
  PROP_MIN_SIZE_PERCENTAGE
};

#define PROP_ENCODING_PRESET_DEFAULT      XDM_HIGH_SPEED
#define PROP_RATE_CONTROL_DEFAULT         IVIDEO_RATECONTROLPRESET_DEFAULT
#define PROP_MAX_BITRATE_DEFAULT          6000000
#define PROP_TARGET_BITRATE_DEFAULT       6000000
#define PROP_INTRA_FRAME_INTERVAL_DEFAULT 30
#define PROP_FORCE_FRAME_DEFAULT          IVIDEO_NA_FRAME
#define PROP_NUM_OUT_BUFFERS_DEFAULT      3
#define PROP_MIN_SIZE_PERCENTAGE_DEFAULT  100

#define GST_CE_VIDENC_RATE_CONTROL_TYPE (gst_ce_videnc_rate_control_get_type())
static GType
gst_ce_videnc_rate_control_get_type (void)
{
  static GType rate_type = 0;

  static const GEnumValue rate_types[] = {
    {IVIDEO_LOW_DELAY, "Constant Bit Rate, for video conferencing", "CBR"},
    {IVIDEO_STORAGE, "Variable Bit Rate, for storage", "VBR"},
    {IVIDEO_TWOPASS, "Two pass rate, for non real-time applications",
        "Two Pass"},
    {IVIDEO_NONE, "No Rate Control is used", "None"},
    {IVIDEO_USER_DEFINED, "User defined on algorithm specific properties",
        "User"},
    {0, NULL, NULL}
  };

  if (!rate_type) {
    rate_type = g_enum_register_static ("GstCeVidEncRate", rate_types);
  }
  return rate_type;
}

#define GST_CE_VIDENC_ENCODING_PRESET_TYPE (gst_ce_videnc_preset_get_type())
static GType
gst_ce_videnc_preset_get_type (void)
{
  static GType preset_type = 0;

  static const GEnumValue preset_types[] = {
    {XDM_HIGH_QUALITY, "High quality", "quality"},
    {XDM_HIGH_SPEED, "High speed, for storage", "speed"},
    {XDM_USER_DEFINED, "User defined on algorithm specific properties", "user"},
    {0, NULL, NULL}
  };

  if (!preset_type) {
    preset_type = g_enum_register_static ("GstCeVidEncPreset", preset_types);
  }
  return preset_type;
}

#define GST_CE_VIDENC_FORCE_FRAME_TYPE (gst_ce_videnc_force_frame_get_type())
static GType
gst_ce_videnc_force_frame_get_type (void)
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
        g_enum_register_static ("GstCeVidEncForce", force_frame_types);
  }
  return force_frame_type;
}

#define GST_CEVIDENC_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_CEVIDENC, GstCeVidEncPrivate))

struct _GstCeVidEncPrivate
{
  gboolean first_buffer;
  gboolean interlace;

  /* Video Data */
  gint fps_num;
  gint fps_den;
  gint par_num;
  gint par_den;
  gint bpp;

  gint32 outbuf_size;
  guint outbuf_size_percentage;
  gint num_out_buffers;
  GstBufferPool *outbuf_pool;

  GstVideoFormat video_format;
  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;

  /* Handle to the CMEM allocator */
  GstAllocator *allocator;
  GstAllocationParams alloc_params;

  /* Codec Data */
  Engine_Handle engine_handle;
  IVIDEO1_BufDescIn inbuf_desc;
  XDM_BufDesc outbuf_desc;
};

/* A number of function prototypes are given so we can refer to them later. */
static gboolean gst_ce_videnc_open (GstVideoEncoder * encoder);
static gboolean gst_ce_videnc_close (GstVideoEncoder * encoder);
static gboolean gst_ce_videnc_stop (GstVideoEncoder * encoder);
static gboolean gst_ce_videnc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static gboolean gst_ce_videnc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);
static gboolean gst_ce_videnc_decide_allocation (GstVideoEncoder * encoder,
    GstQuery * query);
static GstFlowReturn gst_ce_videnc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);

static void gst_ce_videnc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ce_videnc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_ce_videnc_finalize (GObject * object);

static gboolean gst_ce_videnc_reset (GstVideoEncoder * encoder);
static gboolean gst_ce_videnc_set_dynamic_params (GstCeVidEnc * ce_videnc);
static gboolean gst_ce_videnc_get_buffer_info (GstCeVidEnc * ce_videnc);

#define gst_ce_videnc_parent_class parent_class
G_DEFINE_TYPE (GstCeVidEnc, gst_ce_videnc, GST_TYPE_VIDEO_ENCODER);

static void
gst_ce_videnc_class_init (GstCeVidEncClass * klass)
{
  GObjectClass *gobject_class;
  GstVideoEncoderClass *venc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  venc_class = GST_VIDEO_ENCODER_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_ce_videnc_debug, "ce_videnc", 0,
      "CE videnc element");

  g_type_class_add_private (klass, sizeof (GstCeVidEncPrivate));

  /* 
   * Not using GST_DEBUG_FUNCPTR with GObject
   * virtual functions because no one will use them
   */
  gobject_class->set_property = gst_ce_videnc_set_property;
  gobject_class->get_property = gst_ce_videnc_get_property;
  gobject_class->finalize = gst_ce_videnc_finalize;

  g_object_class_install_property (gobject_class, PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control", "Encoding rate control",
          "Encoding rate control", GST_CE_VIDENC_RATE_CONTROL_TYPE,
          PROP_RATE_CONTROL_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ENCODING_PRESET,
      g_param_spec_enum ("encoding-preset", "Encoding preset",
          "Encoding preset", GST_CE_VIDENC_ENCODING_PRESET_TYPE,
          PROP_ENCODING_PRESET_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_BITRATE,
      g_param_spec_int ("max-bitrate",
          "Maximum bit rate",
          "Maximum bit rate to be supported in bits per second",
          1000, 50000000, PROP_MAX_BITRATE_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_TARGET_BITRATE,
      g_param_spec_int ("target-bitrate",
          "Target bit rate",
          "Target bit rate in bits per second, should be <= than the maxbitrate",
          1000, 20000000, PROP_TARGET_BITRATE_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_INTRA_FRAME_INTERVAL,
      g_param_spec_int ("intraframe-interval",
          "Intra frame interval",
          "Interval between two consecutive intra frames",
          0, G_MAXINT32, PROP_INTRA_FRAME_INTERVAL_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_FORCE_FRAME,
      g_param_spec_enum ("force-frame",
          "Force frame",
          "Force next frame to be encoded as a specific type",
          GST_CE_VIDENC_FORCE_FRAME_TYPE, PROP_FORCE_FRAME_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NUM_OUT_BUFFERS,
      g_param_spec_int ("num-out-buffers",
          "Number of output buffers",
          "Number of buffers to be used in the output buffer pool",
          3, G_MAXINT32, PROP_NUM_OUT_BUFFERS_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_MIN_SIZE_PERCENTAGE,
      g_param_spec_int ("min-size-percentage",
          "Minimum output buffer size percentaje",
          "Define the minimum size acceptable for an output buffer,"
          "as a percentage of the input buffer size recomended by the encoder."
          "The encoder will use the defined smaller buffer when there "
          "is not enough free memory. Only set this property to less than 100 if you can "
          "ensure the encoder will compress the data enough to fit in the smaller buffer "
          "and you don't want to drop buffers",
          10, 100, PROP_MIN_SIZE_PERCENTAGE_DEFAULT, G_PARAM_READWRITE));

  venc_class->open = GST_DEBUG_FUNCPTR (gst_ce_videnc_open);
  venc_class->close = GST_DEBUG_FUNCPTR (gst_ce_videnc_close);
  venc_class->stop = GST_DEBUG_FUNCPTR (gst_ce_videnc_stop);
  venc_class->handle_frame = GST_DEBUG_FUNCPTR (gst_ce_videnc_handle_frame);
  venc_class->set_format = GST_DEBUG_FUNCPTR (gst_ce_videnc_set_format);
  venc_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_ce_videnc_propose_allocation);
  venc_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ce_videnc_decide_allocation);

  gobject_class->finalize = gst_ce_videnc_finalize;
}

static void
gst_ce_videnc_init (GstCeVidEnc * ce_videnc)
{
  GstCeVidEncPrivate *priv;

  GST_DEBUG_OBJECT (ce_videnc, "initialize encoder");

  priv = ce_videnc->priv = GST_CEVIDENC_GET_PRIVATE (ce_videnc);

  /* Allocate the codec params */
  if (!ce_videnc->codec_params) {
    GST_DEBUG_OBJECT (ce_videnc, "allocating codec params");
    ce_videnc->codec_params = g_malloc0 (sizeof (VIDENC1_Params));
    if (!ce_videnc->codec_params) {
      GST_ELEMENT_ERROR (ce_videnc, RESOURCE, NO_SPACE_LEFT,
          (("failed to allocate VIDENC1_Params")), (NULL));

      return;
    }
    ce_videnc->codec_params->size = sizeof (VIDENC1_Params);
  }

  if (!ce_videnc->codec_dyn_params) {
    GST_DEBUG_OBJECT (ce_videnc, "allocating codec dynamic params");
    ce_videnc->codec_dyn_params = g_malloc0 (sizeof (VIDENC1_DynamicParams));
    if (!ce_videnc->codec_dyn_params) {
      GST_ELEMENT_ERROR (ce_videnc, RESOURCE, NO_SPACE_LEFT,
          (("failed to allocate VIDENC1_DynamicParams")), (NULL));
      return;
    }
    ce_videnc->codec_dyn_params->size = sizeof (VIDENC1_DynamicParams);
  }

  priv->first_buffer = TRUE;
  priv->engine_handle = NULL;
  priv->allocator = NULL;
  priv->interlace = FALSE;

  gst_ce_videnc_reset ((GstVideoEncoder *) ce_videnc);
}

static void
gst_ce_videnc_finalize (GObject * object)
{
  GstCeVidEnc *ce_videnc = GST_CEVIDENC (object);

  /* Free the allocated resources */
  if (ce_videnc->codec_params) {
    g_free (ce_videnc->codec_params);
    ce_videnc->codec_params = NULL;
  }

  if (ce_videnc->codec_dyn_params) {
    g_free (ce_videnc->codec_dyn_params);
    ce_videnc->codec_dyn_params = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_ce_videnc_configure_codec:
 * Based on the negotiated format, create and initialize the 
 * codec instance*/
static gboolean
gst_ce_videnc_configure_codec (GstCeVidEnc * ce_videnc)
{
  GstCeVidEncClass *klass;
  GstCeVidEncPrivate *priv;
  VIDENC1_Status enc_status;
  VIDENC1_Params *params;
  VIDENC1_DynamicParams *dyn_params;
  gint fps;

  klass = GST_CEVIDENC_CLASS (G_OBJECT_GET_CLASS (ce_videnc));
  priv = ce_videnc->priv;
  params = ce_videnc->codec_params;
  dyn_params = ce_videnc->codec_dyn_params;

  g_return_val_if_fail (params, FALSE);
  g_return_val_if_fail (dyn_params, FALSE);
  g_return_val_if_fail (klass->codec_name, FALSE);

  GST_OBJECT_LOCK (ce_videnc);

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
      GST_ELEMENT_ERROR (ce_videnc, STREAM, NOT_IMPLEMENTED,
          ("unsupported format in video stream: %d\n",
              priv->video_format), (NULL));
      return FALSE;
  }

  if (priv->interlace) {
    params->inputContentType = IVIDEO_INTERLACED;
    dyn_params->captureWidth = dyn_params->captureWidth << 1;
  }

  fps = (priv->fps_num * 1000) / priv->fps_den;

  params->maxWidth = priv->inbuf_desc.frameWidth;
  params->maxHeight = priv->inbuf_desc.frameHeight;
  params->maxFrameRate = fps;

  dyn_params->inputWidth = priv->inbuf_desc.frameWidth;
  dyn_params->inputHeight = priv->inbuf_desc.frameHeight;
  dyn_params->refFrameRate = dyn_params->targetFrameRate = fps;

  if (ce_videnc->codec_handle) {
    /* TODO: test this use case to verify its properly handled */
    GST_DEBUG_OBJECT (ce_videnc, "Closing old codec session");
    VIDENC1_delete (ce_videnc->codec_handle);
  }

  GST_DEBUG_OBJECT (ce_videnc, "Create the codec handle");
  ce_videnc->codec_handle = VIDENC1_create (priv->engine_handle,
      (Char *) klass->codec_name, params);
  if (!ce_videnc->codec_handle)
    goto fail_open_codec;

  enc_status.size = sizeof (VIDENC1_Status);
  enc_status.data.buf = NULL;

  GST_DEBUG_OBJECT (ce_videnc, "Set codec dynamic parameters");
  if (!gst_ce_videnc_set_dynamic_params (ce_videnc))
    goto fail_out;

  if (!gst_ce_videnc_get_buffer_info (ce_videnc))
    goto fail_out;

  GST_OBJECT_UNLOCK (ce_videnc);

  return TRUE;

fail_open_codec:
  {
    GST_ERROR_OBJECT (ce_videnc, "failed to open codec %s", klass->codec_name);
    GST_OBJECT_UNLOCK (ce_videnc);
    return FALSE;
  }
fail_out:
  {
    GST_OBJECT_UNLOCK (ce_videnc);
    VIDENC1_delete (ce_videnc->codec_handle);
    ce_videnc->codec_handle = NULL;
    return FALSE;
  }
}

static gboolean
gst_ce_videnc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  GstCaps *allowed_caps;
  GstBuffer *codec_data = NULL;
  gint i, bpp = 0;

  GstCeVidEnc *ce_videnc = GST_CEVIDENC (encoder);
  GstCeVidEncClass *klass = GST_CEVIDENC_CLASS (G_OBJECT_GET_CLASS (ce_videnc));
  GstCeVidEncPrivate *priv = ce_videnc->priv;

  GST_DEBUG_OBJECT (ce_videnc, "extracting common video information");

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

  GST_DEBUG_OBJECT (ce_videnc, "input buffer format: width=%li, height=%li,"
      " pitch=%li, bpp=%d", priv->inbuf_desc.frameWidth,
      priv->inbuf_desc.frameHeight, priv->inbuf_desc.framePitch, priv->bpp);

  if (!gst_ce_videnc_configure_codec (ce_videnc))
    goto fail_set_caps;

  /* some codecs support more than one format, first auto-choose one */
  GST_DEBUG_OBJECT (ce_videnc, "choosing an output format...");
  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  if (!allowed_caps) {
    GST_DEBUG_OBJECT (ce_videnc, "... but no peer, using template caps");
    allowed_caps =
        gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  }
  GST_DEBUG_OBJECT (ce_videnc, "chose caps %" GST_PTR_FORMAT, allowed_caps);

  if (klass->set_src_caps) {
    GST_DEBUG ("use custom set src caps");
    if (!klass->set_src_caps (ce_videnc, &allowed_caps, &codec_data))
      goto fail_set_caps;
  }

  /* Truncate to the first structure and fixate any unfixed fields */
  allowed_caps = gst_caps_fixate (allowed_caps);

  /* Store input state and set output state */
  if (priv->input_state)
    gst_video_codec_state_unref (priv->input_state);
  priv->input_state = gst_video_codec_state_ref (state);

  /* Set output state */
  if (priv->output_state)
    gst_video_codec_state_unref (priv->output_state);

  priv->output_state =
      gst_video_encoder_set_output_state (encoder, allowed_caps, state);
  if (!priv->output_state) {
    goto fail_set_caps;
  }

  if (codec_data) {
    GST_DEBUG_OBJECT (ce_videnc, "setting the codec data");
    priv->output_state->codec_data = codec_data;
  }

  return TRUE;

fail_set_caps:
  GST_ERROR_OBJECT (ce_videnc, "couldn't set video format");
  return FALSE;
}

static gboolean
gst_ce_videnc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  GstCeVidEnc *ce_videnc = GST_CEVIDENC (encoder);
  GstCeVidEncPrivate *priv = ce_videnc->priv;
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

static gboolean
gst_ce_videnc_decide_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  GstCeVidEnc *ce_videnc = (GstCeVidEnc *) encoder;
  GstCeVidEncPrivate *priv = ce_videnc->priv;
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstCaps *caps;

  GST_LOG_OBJECT (ce_videnc, "decide allocation");
  if (!GST_VIDEO_ENCODER_CLASS (parent_class)->decide_allocation (encoder,
          query))
    return FALSE;

  /* use our own pool */
  pool = priv->outbuf_pool;
  if (!pool)
    return FALSE;

  /* we got configuration from our peer or the decide_allocation method,
   * parse them */
  if (gst_query_get_n_allocation_params (query) > 0)
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
  else
    gst_allocation_params_init (&params);

  if (params.align < 31)
    params.align = 31;

  GST_DEBUG_OBJECT (ce_videnc, "allocation params %d, %d %d, %d", params.flags,
      params.align, params.padding, params.prefix);
  priv->alloc_params = params;

  if (priv->output_state)
    caps = priv->output_state->caps;

  GST_DEBUG_OBJECT (ce_videnc, "configuring output pool");
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, priv->outbuf_size, 1,
      priv->num_out_buffers);
  gst_buffer_pool_config_set_allocator (config, priv->allocator,
      &priv->alloc_params);
  gst_buffer_pool_set_config (GST_BUFFER_POOL_CAST (pool), config);
  gst_buffer_pool_set_active (GST_BUFFER_POOL_CAST (pool), TRUE);

  gst_ce_slice_buffer_pool_set_min_size (GST_CE_SLICE_BUFFER_POOL_CAST (pool),
      priv->outbuf_size_percentage, TRUE);
  return TRUE;
}

/*
 * gst_ce_videnc_allocate_output_frame
 * 
 * Allocates a CMEM output buffer
 */
static gboolean
gst_ce_videnc_allocate_output_frame (GstCeVidEnc * ce_videnc, GstBuffer ** buf)
{
  GstCeVidEncPrivate *priv = ce_videnc->priv;

  /* Get allocator parameters */
  gst_video_encoder_get_allocator ((GstVideoEncoder *) ce_videnc, NULL,
      &priv->alloc_params);

  *buf = gst_buffer_new_allocate (priv->allocator, priv->outbuf_size,
      &priv->alloc_params);

  if (!*buf) {
    GST_DEBUG_OBJECT (ce_videnc, "Can't alloc output buffer");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn 
gst_ce_videnc_encode_buffer (GstCeVidEnc *ce_videnc, GstBuffer **outbuf, VIDENC1_OutArgs *out_args) 
{
  GstCeVidEncPrivate *priv = ce_videnc->priv;


  GstMapInfo info_out;
  VIDENC1_InArgs in_args;
  gint ret = 0;

  /* Allocate output buffer */
  if (gst_buffer_pool_acquire_buffer (GST_BUFFER_POOL_CAST (priv->outbuf_pool),
          outbuf, NULL) != GST_FLOW_OK) {
    *outbuf = NULL;
    goto fail_alloc;
  }

  if (!gst_buffer_map (*outbuf, &info_out, GST_MAP_WRITE))
    goto fail_map;

  priv->outbuf_desc.bufs = (XDAS_Int8 **) & (info_out.data);

  /* Set output and input arguments for the encoding process */
  in_args.size = sizeof (IVIDENC1_InArgs);
  in_args.inputID = 1;
  in_args.topFieldFirstFlag = 1;

  out_args->size = sizeof (VIDENC1_OutArgs);

  /* Encode process */
  ret =
      VIDENC1_process (ce_videnc->codec_handle, &priv->inbuf_desc,
      &priv->outbuf_desc, &in_args, out_args);
  if (ret != VIDENC1_EOK)
    goto fail_encode;

  GST_DEBUG_OBJECT (ce_videnc,
      "encoded an output buffer %p of size %li at addr %p", outbuf,
      out_args->bytesGenerated, *priv->outbuf_desc.bufs);

  gst_buffer_unmap (*outbuf, &info_out);

  gst_ce_slice_buffer_resize (GST_CE_SLICE_BUFFER_POOL_CAST (priv->outbuf_pool),
      *outbuf, out_args->bytesGenerated);

  return GST_FLOW_OK;

  /*ERRORS*/
fail_alloc:
  {
    GST_INFO_OBJECT (ce_videnc, "Failed to get output buffer, frame dropped");
    return GST_FLOW_ERROR;
  }
fail_map:
  {
    GST_ERROR_OBJECT (ce_videnc, "Failed to map buffer");
    return GST_FLOW_ERROR;
  }
fail_encode:
  {
    gst_buffer_unmap (*outbuf, &info_out);
    GST_ERROR_OBJECT (ce_videnc,
        "Failed encode process with extended error: 0x%x",
        (unsigned int) out_args->extendedError);
    return GST_FLOW_ERROR;
  }
}
static GstFlowReturn
gst_ce_videnc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstCeVidEnc *ce_videnc = GST_CEVIDENC (encoder);
  GstCeVidEncPrivate *priv = ce_videnc->priv;
  GstCeVidEncClass *klass = GST_CEVIDENC_CLASS (G_OBJECT_GET_CLASS (ce_videnc));

  GstVideoInfo *info = &priv->input_state->info;
  GstVideoFrame vframe;
  GstBuffer *outbuf = NULL;
  GstFlowReturn ret;
  VIDENC1_OutArgs out_args;

  gint i,j;
  gint fields;
  gint current_pitch;

  /* $
   * TODO
   * Failing if input buffer is not contiguous. Should it copy the
   * buffer instead?
   */
  if (!gst_ce_is_buffer_contiguous (frame->input_buffer))
    goto fail_no_contiguous_buffer;

  /* Fill planes pointer */
  if (!gst_video_frame_map (&vframe, info, frame->input_buffer, GST_MAP_READ))
    goto fail_map;

  current_pitch = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);

  if (priv->inbuf_desc.framePitch != current_pitch) {
    priv->inbuf_desc.framePitch = current_pitch;
    switch (priv->video_format) {
      case GST_VIDEO_FORMAT_UYVY:
        ce_videnc->codec_dyn_params->captureWidth =
            priv->inbuf_desc.framePitch / 2;
        break;
      case GST_VIDEO_FORMAT_NV12:
        ce_videnc->codec_dyn_params->captureWidth = priv->inbuf_desc.framePitch;
        break;
      default:
        ce_videnc->codec_dyn_params->captureWidth = 0;

    }

    if (ce_videnc->codec_params->inputContentType) {
      ce_videnc->codec_dyn_params->captureWidth = ce_videnc->codec_dyn_params->captureWidth << 1;
    }

    if (!gst_ce_videnc_set_dynamic_params (ce_videnc))
      goto fail_set_buffer_stride;

    if (!gst_ce_videnc_get_buffer_info (ce_videnc))
      goto fail_set_buffer_stride;
  }

  /* Making sure the output buffer pool is configured */
  if (priv->first_buffer && gst_pad_check_reconfigure (encoder->srcpad))
    gst_video_encoder_negotiate (GST_VIDEO_ENCODER (encoder));

  /* Pre-encode process */
  if (klass->pre_process
      && !klass->pre_process (ce_videnc, frame->input_buffer))
    goto fail_pre_encode;

  /* Encode process */
  for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (&vframe); i++) {
    priv->inbuf_desc.bufDesc[i].buf = GST_VIDEO_FRAME_PLANE_DATA (&vframe, i);
  }

  /* Get oldest frame */
  gst_video_codec_frame_unref (frame);
  frame = gst_video_encoder_get_oldest_frame (encoder);

  fields = 1 << (ce_videnc->codec_params->inputContentType);
  for (j=1; j <= fields; j++) {
    if (gst_ce_videnc_encode_buffer(ce_videnc, &outbuf, &out_args) != GST_FLOW_OK) {
      if (outbuf == NULL) {
	frame->output_buffer = NULL;
	gst_video_encoder_finish_frame (encoder, frame); 
	goto drop_buffer;
      }
      goto fail_encode;
    }

    /* Post-encode process */
    if (klass->post_process && !klass->post_process (ce_videnc, outbuf))
      goto fail_post_encode;

    if (frame->output_buffer)
      gst_buffer_unref (frame->output_buffer);

    frame->output_buffer = outbuf;

    /* Mark I and IDR frames */
    if ((out_args.encodedFrameType == IVIDEO_I_FRAME) ||
	(out_args.encodedFrameType == IVIDEO_IDR_FRAME)) {
      GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
    }

    if (j != fields) {
      /* Initialize odd field process*/
      for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (&vframe); i++) {
	priv->inbuf_desc.bufDesc[i].buf += current_pitch;
      }
      gst_video_codec_frame_ref(frame);

    }

    ret = gst_video_encoder_finish_frame (encoder, frame);
    if (ret != GST_FLOW_OK)
      goto out;
  }

  gst_video_frame_unmap (&vframe);

  GST_DEBUG_OBJECT (ce_videnc, "frame encoded succesfully");

out:
  return ret;

 drop_buffer:
  {
    GST_WARNING_OBJECT (ce_videnc, "Couldn't get output memory, dropping buffer");
    return GST_FLOW_OK;
  }
fail_map:
  {
    GST_ERROR_OBJECT (encoder, "Failed to map input buffer");
    return GST_FLOW_ERROR;
  }
fail_set_buffer_stride:
  {
    GST_ERROR_OBJECT (encoder, "Failed to set buffer stride");
    return GST_FLOW_ERROR;
  }
fail_no_contiguous_buffer:
  {
    GST_ERROR_OBJECT (encoder, "Input buffer should be contiguous");
    return GST_FLOW_ERROR;
  }
fail_pre_encode:
  {
    GST_ERROR_OBJECT (ce_videnc, "Failed pre-encode process");
    return GST_FLOW_ERROR;
  }
fail_encode:
  {
    GST_ERROR_OBJECT (ce_videnc, "Failed encode process");
    return GST_FLOW_ERROR;
  }
fail_post_encode:
  {
    GST_ERROR_OBJECT (ce_videnc, "Failed post-encode process");
    return GST_FLOW_ERROR;
  }
}

static void
gst_ce_videnc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstCeVidEnc *ce_videnc;
  GstCeVidEncClass *klass;
  VIDENC1_Params *params;
  VIDENC1_DynamicParams *dyn_params;
  gboolean set_params = FALSE;

  /* Get a pointer of the right type. */
  ce_videnc = GST_CEVIDENC (object);
  klass = GST_CEVIDENC_CLASS (G_OBJECT_GET_CLASS (ce_videnc));

  if ((!ce_videnc->codec_params) || (!ce_videnc->codec_dyn_params)) {
    GST_WARNING_OBJECT (ce_videnc,
        "couldn't set property, no codec parameters defined");
    return;
  }

  params = (VIDENC1_Params *) ce_videnc->codec_params;
  dyn_params = (VIDENC1_DynamicParams *) ce_videnc->codec_dyn_params;

  GST_OBJECT_LOCK (ce_videnc);
  /* Check the argument id to see which argument we're setting. */
  switch (prop_id) {

    case PROP_RATE_CONTROL:
      if (!ce_videnc->codec_handle) {
        params->rateControlPreset = g_value_get_enum (value);
        GST_LOG_OBJECT (ce_videnc,
            "setting encoding rate control to %li", params->rateControlPreset);
      } else {
        goto fail_static_prop;
      }
      break;
    case PROP_ENCODING_PRESET:
      if (!ce_videnc->codec_handle) {
        params->encodingPreset = g_value_get_enum (value);
        GST_LOG_OBJECT (ce_videnc,
            "setting encoding rate control to %li", params->encodingPreset);
      } else {
        goto fail_static_prop;
      }
      break;
    case PROP_MAX_BITRATE:
      if (!ce_videnc->codec_handle) {
        params->maxBitRate = g_value_get_int (value);
        GST_LOG_OBJECT (ce_videnc,
            "setting max bitrate to %li", params->maxBitRate);
      } else {
        goto fail_static_prop;
      }
      break;
    case PROP_TARGET_BITRATE:
      dyn_params->targetBitRate = g_value_get_int (value);
      GST_LOG_OBJECT (ce_videnc,
          "setting target bitrate to %li", dyn_params->targetBitRate);
      set_params = TRUE;
      break;
    case PROP_INTRA_FRAME_INTERVAL:
      dyn_params->intraFrameInterval = g_value_get_int (value);
      GST_LOG_OBJECT (ce_videnc,
          "setting intra frame interval to %li",
          dyn_params->intraFrameInterval);
      set_params = TRUE;
      break;
    case PROP_FORCE_FRAME:
      dyn_params->forceFrame = g_value_get_int (value);
      GST_LOG_OBJECT (ce_videnc, "forcing frame to %li",
          dyn_params->forceFrame);
      set_params = TRUE;
      break;
    case PROP_NUM_OUT_BUFFERS:
      ce_videnc->priv->num_out_buffers = g_value_get_int (value);
      GST_LOG_OBJECT (ce_videnc,
          "setting number of output buffers to %d",
          ce_videnc->priv->num_out_buffers);
      break;
    case PROP_MIN_SIZE_PERCENTAGE:
      ce_videnc->priv->outbuf_size_percentage = g_value_get_int (value);
      GST_LOG_OBJECT (ce_videnc,
          "setting min output buffer size percentage to %d",
          ce_videnc->priv->outbuf_size_percentage);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  if (set_params && ce_videnc->codec_handle)
    gst_ce_videnc_set_dynamic_params (ce_videnc);

  GST_OBJECT_UNLOCK (ce_videnc);
  return;

fail_static_prop:
  GST_WARNING_OBJECT (ce_videnc, "can't set static property when "
      "the codec is already configured");
  GST_OBJECT_UNLOCK (ce_videnc);
}

/* The set function is simply the inverse of the get fuction. */
static void
gst_ce_videnc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstCeVidEnc *ce_videnc;
  GstCeVidEncClass *klass;
  VIDENC1_Params *params;
  VIDENC1_DynamicParams *dyn_params;

  /* It's not null if we got it, but it might not be ours */
  ce_videnc = GST_CEVIDENC (object);
  klass = (GstCeVidEncClass *) G_OBJECT_GET_CLASS (ce_videnc);

  if ((!ce_videnc->codec_params) || (!ce_videnc->codec_dyn_params)) {
    GST_WARNING_OBJECT (ce_videnc, "couldn't set property");
    return;
  }

  params = (VIDENC1_Params *) ce_videnc->codec_params;
  dyn_params = (VIDENC1_DynamicParams *) ce_videnc->codec_dyn_params;

  GST_OBJECT_LOCK (ce_videnc);
  switch (prop_id) {
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, params->rateControlPreset);
      break;
    case PROP_ENCODING_PRESET:
      g_value_set_enum (value, params->encodingPreset);
      break;
    case PROP_MAX_BITRATE:
      g_value_set_int (value, params->maxBitRate);
      break;
    case PROP_TARGET_BITRATE:
      g_value_set_int (value, dyn_params->targetBitRate);
      break;
    case PROP_INTRA_FRAME_INTERVAL:
      g_value_set_int (value, dyn_params->intraFrameInterval);
      break;
    case PROP_FORCE_FRAME:
      g_value_set_enum (value, dyn_params->forceFrame);
      break;
    case PROP_NUM_OUT_BUFFERS:
      g_value_set_int (value, ce_videnc->priv->num_out_buffers);
      break;
    case PROP_MIN_SIZE_PERCENTAGE:
      g_value_set_int (value, ce_videnc->priv->outbuf_size_percentage);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (ce_videnc);
}

static gboolean
gst_ce_videnc_open (GstVideoEncoder * encoder)
{
  GstCeVidEnc *ce_videnc = GST_CEVIDENC (encoder);
  GstCeVidEncPrivate *priv = ce_videnc->priv;

  GST_DEBUG_OBJECT (ce_videnc, "opening %s Engine", CODEC_ENGINE);
  /* reset, load, and start DSP Engine */
  if ((priv->engine_handle =
          Engine_open ((Char *) CODEC_ENGINE, NULL, NULL)) == NULL)
    goto fail_engine_open;

  if (priv->allocator)
    gst_object_unref (priv->allocator);

  GST_DEBUG_OBJECT (ce_videnc, "getting CMEM allocator");
  priv->allocator = gst_allocator_find ("ContiguousMemory");

  if (!priv->allocator)
    goto fail_no_allocator;

  GST_DEBUG_OBJECT (ce_videnc, "creating slice buffer pool");

  if (!(priv->outbuf_pool = gst_ce_slice_buffer_pool_new ()))
    goto fail_pool;

  return TRUE;

  /* Errors */
fail_engine_open:
  {
    GST_ELEMENT_ERROR (ce_videnc, STREAM, CODEC_NOT_FOUND, (NULL),
        ("failed to open codec engine \"%s\"", CODEC_ENGINE));
    return FALSE;
  }
fail_no_allocator:
  {
    GST_WARNING_OBJECT (ce_videnc, "can't find the CMEM allocator");
    return FALSE;
  }
fail_pool:
  {
    GST_WARNING_OBJECT (ce_videnc, "can't create slice buffer pool");
    return FALSE;
  }
  return TRUE;
}

static gboolean
gst_ce_videnc_close (GstVideoEncoder * encoder)
{
  GstCeVidEnc *ce_videnc = GST_CEVIDENC (encoder);
  GstCeVidEncPrivate *priv = ce_videnc->priv;

  if (priv->engine_handle) {
    GST_DEBUG_OBJECT (ce_videnc, "closing codec engine %p\n",
        priv->engine_handle);
    Engine_close (priv->engine_handle);
    priv->engine_handle = NULL;
  }

  if (priv->allocator) {
    gst_object_unref (priv->allocator);
    priv->allocator = NULL;
  }

  if (priv->outbuf_pool) {
    gst_object_unref (priv->outbuf_pool);
    priv->outbuf_pool = NULL;
  }

  return TRUE;
}

static gboolean
gst_ce_videnc_stop (GstVideoEncoder * encoder)
{
  return gst_ce_videnc_reset (encoder);
}

static gboolean
gst_ce_videnc_reset (GstVideoEncoder * encoder)
{
  GstCeVidEnc *ce_videnc = GST_CEVIDENC (encoder);
  GstCeVidEncPrivate *priv = ce_videnc->priv;
  GstCeVidEncClass *klass = (GstCeVidEncClass *) G_OBJECT_GET_CLASS (ce_videnc);

  VIDENC1_Params *params = ce_videnc->codec_params;
  VIDENC1_DynamicParams *dyn_params = ce_videnc->codec_dyn_params;

  g_return_val_if_fail (params, FALSE);
  g_return_val_if_fail (dyn_params, FALSE);

  if (priv->input_state) {
    gst_video_codec_state_unref (priv->input_state);
    priv->input_state = NULL;
  }

  if (priv->output_state) {
    gst_video_codec_state_unref (priv->output_state);
    priv->output_state = NULL;
  }

  if (ce_videnc->codec_handle) {
    VIDENC1_delete (ce_videnc->codec_handle);
    ce_videnc->codec_handle = NULL;
  }

  GST_OBJECT_LOCK (ce_videnc);

  priv->num_out_buffers = PROP_NUM_OUT_BUFFERS_DEFAULT;
  priv->outbuf_size_percentage = PROP_MIN_SIZE_PERCENTAGE_DEFAULT;
  /* Set default values for codec static params */
  params->encodingPreset = PROP_ENCODING_PRESET_DEFAULT;
  params->rateControlPreset = PROP_RATE_CONTROL_DEFAULT;
  params->maxBitRate = PROP_MAX_BITRATE_DEFAULT;
  params->dataEndianness = XDM_BYTE;
  params->maxInterFrameInterval = 1;
  params->inputChromaFormat = XDM_YUV_420P;
  params->inputContentType = IVIDEO_PROGRESSIVE;
  params->reconChromaFormat = XDM_CHROMA_NA;

  /* Set default values for codec dynamic params */
  dyn_params->targetBitRate = PROP_TARGET_BITRATE_DEFAULT;
  dyn_params->intraFrameInterval = PROP_INTRA_FRAME_INTERVAL_DEFAULT;
  dyn_params->generateHeader = XDM_ENCODE_AU;
  dyn_params->captureWidth = 0;
  dyn_params->forceFrame = PROP_FORCE_FRAME_DEFAULT;
  dyn_params->interFrameInterval = 1;
  dyn_params->mbDataFlag = 0;

  GST_OBJECT_UNLOCK (ce_videnc);

  /* Configure specific codec */
  if (klass->reset) {
    GST_DEBUG_OBJECT (ce_videnc, "configuring codec");
    klass->reset (ce_videnc);
  }

  return TRUE;
}

/* Set current dynamic parameters */
static gboolean
gst_ce_videnc_set_dynamic_params (GstCeVidEnc * ce_videnc)
{
  VIDENC1_Status enc_status;
  gint ret;

  g_return_val_if_fail (ce_videnc->codec_handle, FALSE);
  g_return_val_if_fail (ce_videnc->codec_dyn_params, FALSE);

  enc_status.size = sizeof (VIDENC1_Status);
  enc_status.data.buf = NULL;

  ret = VIDENC1_control (ce_videnc->codec_handle, XDM_SETPARAMS,
      ce_videnc->codec_dyn_params, &enc_status);
  if (ret != VIDENC1_EOK) {
    GST_WARNING_OBJECT (ce_videnc, "Failed to set dynamic parameters, "
        "status error %x, %d", (unsigned int) enc_status.extendedError, ret);
    return FALSE;
  }

  return TRUE;
}

/* Get buffer information from video codec */
static gboolean
gst_ce_videnc_get_buffer_info (GstCeVidEnc * ce_videnc)
{
  GstCeVidEncPrivate *priv = ce_videnc->priv;
  VIDENC1_Status enc_status;
  gint i, ret;

  g_return_val_if_fail (ce_videnc->codec_handle, FALSE);
  g_return_val_if_fail (ce_videnc->codec_dyn_params, FALSE);

  enc_status.size = sizeof (VIDENC1_Status);
  enc_status.data.buf = NULL;

  ret = VIDENC1_control (ce_videnc->codec_handle, XDM_GETBUFINFO,
      ce_videnc->codec_dyn_params, &enc_status);
  if (ret != VIDENC1_EOK) {
    GST_ERROR_OBJECT (ce_videnc, "failed to get buffer information, "
        "status error %x, %d", (guint) enc_status.extendedError, ret);
    return FALSE;
  }

  for (i = 0; i < enc_status.bufInfo.minNumInBufs; i++) {
    priv->inbuf_desc.bufDesc[i].bufSize = enc_status.bufInfo.minInBufSize[i];
    GST_DEBUG_OBJECT (ce_videnc, "size of input buffer [%d] = %li", i,
        priv->inbuf_desc.bufDesc[i].bufSize);
  }

  priv->outbuf_size = enc_status.bufInfo.minOutBufSize[0];
  priv->outbuf_desc.numBufs = 1;
  priv->outbuf_desc.bufSizes = (XDAS_Int32 *) & priv->outbuf_size;

  GST_DEBUG_OBJECT (ce_videnc, "output buffer size = %d", priv->outbuf_size);

  return TRUE;
}

/**
 * gst_ce_videnc_get_header:
 * @ce_videnc: a #GstCeVidEnc
 * @buffer: (out) (transfer full): the #GstBuffer containing the 
 *        encoding header.
 * @header_size: (out): the bytes generated for the header.
 * 
 * Lets #GstCeVidEnc sub-classes to obtain the encoding header,
 * that can be used to calculate the corresponding codec data.
 *
 * Unref the @buffer after use it.
 */
gboolean
gst_ce_videnc_get_header (GstCeVidEnc * ce_videnc, GstBuffer ** buffer,
    gint * header_size)
{
  GstCeVidEncPrivate *priv = ce_videnc->priv;
  VIDENC1_InArgs in_args;
  VIDENC1_OutArgs out_args;
  GstBuffer *header_buf;
  GstMapInfo info;
  gint ret;

  g_return_val_if_fail (GST_IS_CEVIDENC (ce_videnc), FALSE);
  g_return_val_if_fail (ce_videnc->codec_handle, FALSE);
  g_return_val_if_fail (ce_videnc->codec_dyn_params, FALSE);

  GST_OBJECT_LOCK (ce_videnc);

  GST_DEBUG_OBJECT (ce_videnc, "get H.264 header");

  ce_videnc->codec_dyn_params->generateHeader = XDM_GENERATE_HEADER;
  if (!gst_ce_videnc_set_dynamic_params (ce_videnc))
    goto fail_out;

  /*Allocate an output buffer for the header */
  header_buf = gst_buffer_new_allocate (priv->allocator, 200,
      &priv->alloc_params);
  if (!gst_buffer_map (header_buf, &info, GST_MAP_WRITE))
    goto fail_out;

  priv->outbuf_desc.bufs = (XDAS_Int8 **) & (info.data);

  /* Set output and input arguments for the encode process */
  in_args.size = sizeof (IVIDENC1_InArgs);
  in_args.inputID = 1;
  in_args.topFieldFirstFlag = 1;

  out_args.size = sizeof (VIDENC1_OutArgs);

  /* Generate the header */
  ret =
      VIDENC1_process (ce_videnc->codec_handle, &priv->inbuf_desc,
      &priv->outbuf_desc, &in_args, &out_args);
  if (ret != VIDENC1_EOK)
    goto fail_encode;

  gst_buffer_unmap (header_buf, &info);

  ce_videnc->codec_dyn_params->generateHeader = XDM_ENCODE_AU;
  if (!gst_ce_videnc_set_dynamic_params (ce_videnc))
    goto fail_out;

  GST_OBJECT_UNLOCK (ce_videnc);

  *header_size = out_args.bytesGenerated;
  *buffer = header_buf;

  return TRUE;

fail_encode:
  {
    gst_buffer_unmap (header_buf, &info);
    GST_OBJECT_UNLOCK (ce_videnc);
    GST_WARNING_OBJECT (ce_videnc,
        "Failed header encode process with extended error: 0x%x",
        (unsigned int) out_args.extendedError);
    return FALSE;
  }

fail_out:
  {
    GST_OBJECT_UNLOCK (ce_videnc);
    if (header_buf)
      gst_buffer_unref (header_buf);
    return FALSE;
  }
}

/**
 * gst_ce_videnc_set_interlace:
 * @ce_videnc: a #GstCeVidEnc
 * @interlace: boolean indicating interlace mode
 * 
 * Enables/Disables interlace encoding. This interlace setting
 * takes effect until the next codec configuration. 
 *
 * When interlace is enabled, input buffers are processed as 
 * interleaved fields, every alternate lines are read as top/bottom field.
 * The codec encodes each field separately, so you will have two
 * output buffers for each input buffer. 
 */
void
gst_ce_videnc_set_interlace (GstCeVidEnc *ce_videnc, gboolean interlace)
{
  g_return_if_fail (GST_IS_CEVIDENC(ce_videnc));

  GST_OBJECT_LOCK (ce_videnc);

  ce_videnc->priv->interlace = interlace;

  GST_DEBUG_OBJECT (ce_videnc, "set interlace %d", ce_videnc->priv->interlace);
  GST_OBJECT_UNLOCK (ce_videnc);
}
