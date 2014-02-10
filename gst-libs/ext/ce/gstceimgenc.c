/*
 * gstceimgenc.c
 *
 * Copyright (C) 2013 RidgeRun, LLC (http://www.ridgerun.com)
 * Author: Carlos Gomez Viquez <carlos.gomez@ridgerun.com>
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
 * SECTION:gstceimgenc
 * @short_description: Base class for Codec Engine image encoders
 * @see_also:
 *
 * This base class is for image encoders turning raw video into
 * encoded image data using TI codecs with the IMGENC1 image encoding
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

#include "gstceimgenc.h"

#include <ti/sdo/ce/osal/Memory.h>

GST_DEBUG_CATEGORY_STATIC (gst_ce_imgenc_debug);
#define GST_CAT_DEFAULT gst_ce_imgenc_debug

enum
{
  PROP_0,
  PROP_QUALITY_VALUE,
  PROP_NUM_OUT_BUFFERS,
  PROP_MIN_SIZE_PERCENTAGE
};

#define PROP_QUALITY_VALUE_DEFAULT            75
#define PROP_NUM_OUT_BUFFERS_DEFAULT          3
#define PROP_MIN_SIZE_PERCENTAGE_DEFAULT      100

#define GST_CE_IMGENC_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_CE_IMGENC, GstCeImgEncPrivate))

struct _GstCeImgEncPrivate
{
  gboolean first_buffer;

  /* Basic properties */
  gint32 frame_width;
  gint32 frame_height;
  gint32 frame_pitch;

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
  XDM1_BufDesc inbuf_desc;
  XDM1_BufDesc outbuf_desc;
};

/* A number of function prototypes are given so we can refer to them later */
static gboolean gst_ce_imgenc_open (GstVideoEncoder * encoder);
static gboolean gst_ce_imgenc_close (GstVideoEncoder * encoder);
static gboolean gst_ce_imgenc_stop (GstVideoEncoder * encoder);
static gboolean gst_ce_imgenc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static gboolean gst_ce_imgenc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);
static gboolean gst_ce_imgenc_decide_allocation (GstVideoEncoder * encoder,
    GstQuery * query);
static GstFlowReturn gst_ce_imgenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);

static void gst_ce_imgenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ce_imgenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_ce_imgenc_reset (GstVideoEncoder * encoder);
static void gst_ce_imgenc_finalize (GObject * object);
static gboolean gst_ce_imgenc_set_dynamic_params (GstCeImgEnc * ce_imgenc);
static gboolean gst_ce_imgenc_get_buffer_info (GstCeImgEnc * ce_imgenc);

#define gst_ce_imgenc_parent_class parent_class
G_DEFINE_TYPE (GstCeImgEnc, gst_ce_imgenc, GST_TYPE_VIDEO_ENCODER);

/**
 * Image encoder class initialization function
 */
static void
gst_ce_imgenc_class_init (GstCeImgEncClass * klass)
{
  GObjectClass *gobject_class;
  GstVideoEncoderClass *venc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  venc_class = GST_VIDEO_ENCODER_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_ce_imgenc_debug, "ce_imgenc", 0,
      "CE imgenc element");

  g_type_class_add_private (klass, sizeof (GstCeImgEncPrivate));

  gobject_class->set_property = gst_ce_imgenc_set_property;
  gobject_class->get_property = gst_ce_imgenc_get_property;
  gobject_class->finalize = gst_ce_imgenc_finalize;

  /* Initialization of the image encoder properties */
  g_object_class_install_property (gobject_class, PROP_QUALITY_VALUE,
      g_param_spec_int ("quality-value", "Quality value",
          "Quality factor for encoder", 2, 97,
          PROP_QUALITY_VALUE_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_NUM_OUT_BUFFERS,
      g_param_spec_int ("num-out-buffers",
          "Number of output buffers",
          "Number of buffers to be used in the output buffer pool",
          2, G_MAXINT32, PROP_NUM_OUT_BUFFERS_DEFAULT, G_PARAM_READWRITE));
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

  venc_class->open = GST_DEBUG_FUNCPTR (gst_ce_imgenc_open);
  venc_class->close = GST_DEBUG_FUNCPTR (gst_ce_imgenc_close);
  venc_class->stop = GST_DEBUG_FUNCPTR (gst_ce_imgenc_stop);
  venc_class->handle_frame = GST_DEBUG_FUNCPTR (gst_ce_imgenc_handle_frame);
  venc_class->set_format = GST_DEBUG_FUNCPTR (gst_ce_imgenc_set_format);
  venc_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_ce_imgenc_propose_allocation);
  venc_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ce_imgenc_decide_allocation);
}

/**
 * Image encoder initialization function
 */
static void
gst_ce_imgenc_init (GstCeImgEnc * ce_imgenc)
{
  GstCeImgEncPrivate *priv;

  GST_DEBUG_OBJECT (ce_imgenc, "initialize encoder");

  priv = ce_imgenc->priv = GST_CE_IMGENC_GET_PRIVATE (ce_imgenc);

  /* Allocate the codec parameters */
  if (!ce_imgenc->codec_params) {
    GST_DEBUG_OBJECT (ce_imgenc, "allocating codec params");
    ce_imgenc->codec_params = g_malloc0 (sizeof (IMGENC1_Params));
    if (NULL == ce_imgenc->codec_params) {
      GST_WARNING_OBJECT (ce_imgenc, "failed to allocate IMGENC1_Params");
      return;
    }
    ce_imgenc->codec_params->size = sizeof (IMGENC1_Params);
  }

  /* Allocate the codec dynamic parameters */
  if (!ce_imgenc->codec_dyn_params) {
    GST_DEBUG_OBJECT (ce_imgenc, "allocating codec dynamic params");
    ce_imgenc->codec_dyn_params = g_malloc0 (sizeof (IMGENC1_DynamicParams));
    if (NULL == ce_imgenc->codec_dyn_params) {
      GST_WARNING_OBJECT (ce_imgenc,
          "failed to allocate IMGENC1_DynamicParams");
      return;
    }
    ce_imgenc->codec_dyn_params->size = sizeof (IMGENC1_DynamicParams);
  }

  priv->first_buffer = TRUE;
  priv->engine_handle = NULL;
  priv->allocator = NULL;

  gst_ce_imgenc_reset (GST_VIDEO_ENCODER (ce_imgenc));
}

/**
 * Image encoder class finalization function
 */
static void
gst_ce_imgenc_finalize (GObject * object)
{
  GstCeImgEnc *ce_imgenc = GST_CE_IMGENC (object);

  /* Free the codec parameters */
  if (ce_imgenc->codec_params) {
    g_free (ce_imgenc->codec_params);
    ce_imgenc->codec_params = NULL;
  }

  /* Free the codec dynamic parameters */
  if (ce_imgenc->codec_dyn_params) {
    g_free (ce_imgenc->codec_dyn_params);
    ce_imgenc->codec_dyn_params = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * Based on the negotiated format, creates and initializes the 
 * codec instance
 */
static gboolean
gst_ce_imgenc_configure_codec (GstCeImgEnc * ce_imgenc)
{
  GstCeImgEncClass *klass;
  GstCeImgEncPrivate *priv;
  IMGENC1_Status enc_status;
  IMGENC1_Params *params;
  IMGENC1_DynamicParams *dyn_params;

  klass = GST_CE_IMGENC_CLASS (G_OBJECT_GET_CLASS (ce_imgenc));
  priv = ce_imgenc->priv;
  params = ce_imgenc->codec_params;
  dyn_params = ce_imgenc->codec_dyn_params;

  g_return_val_if_fail (params, FALSE);
  g_return_val_if_fail (dyn_params, FALSE);
  g_return_val_if_fail (klass->codec_name, FALSE);

  GST_OBJECT_LOCK (ce_imgenc);

  /* Set the caps on the dynamic parameters of the encoder */
  switch (priv->video_format) {
    case GST_VIDEO_FORMAT_UYVY:
      dyn_params->inputChromaFormat = XDM_YUV_422ILE;
      dyn_params->captureWidth = priv->frame_pitch / 2;
      break;
    case GST_VIDEO_FORMAT_NV12:
      dyn_params->inputChromaFormat = XDM_YUV_420SP;
      dyn_params->captureWidth = priv->frame_pitch;
      break;
    default:
      GST_ELEMENT_ERROR (ce_imgenc, STREAM, NOT_IMPLEMENTED,
          ("unsupported format in video stream: %d\n",
              priv->video_format), (NULL));
      return FALSE;
  }

  /* Set input frame dimensions */
  params->maxWidth = priv->frame_width;
  params->maxHeight = priv->frame_height;

  dyn_params->inputWidth = priv->frame_width;
  dyn_params->inputHeight = priv->frame_height;

  /* Create the codec handle with the codec parameters given */
  if (ce_imgenc->codec_handle) {
    GST_DEBUG_OBJECT (ce_imgenc, "Closing old codec session");
    IMGENC1_delete (ce_imgenc->codec_handle);
  }

  GST_DEBUG_OBJECT (ce_imgenc, "Create the codec handle");
  ce_imgenc->codec_handle = IMGENC1_create (priv->engine_handle,
      (Char *) klass->codec_name, params);
  if (!ce_imgenc->codec_handle)
    goto fail_open_codec;

  /* Set codec dynamic parameters */
  enc_status.size = sizeof (IMGENC1_Status);
  enc_status.data.buf = NULL;

  GST_DEBUG_OBJECT (ce_imgenc, "Set codec dynamic parameters");
  if (!gst_ce_imgenc_set_dynamic_params (ce_imgenc))
    goto fail_out;

  if (!gst_ce_imgenc_get_buffer_info (ce_imgenc))
    goto fail_out;

  GST_OBJECT_UNLOCK (ce_imgenc);

  return TRUE;

fail_open_codec:
  {
    GST_ERROR_OBJECT (ce_imgenc, "Failed to open codec %s", klass->codec_name);
    GST_OBJECT_UNLOCK (ce_imgenc);
    return FALSE;
  }
fail_out:
  {
    IMGENC1_delete (ce_imgenc->codec_handle);
    ce_imgenc->codec_handle = NULL;
    GST_OBJECT_UNLOCK (ce_imgenc);
    return FALSE;
  }
}

/**
 * Gets the format of input video data from GstVideoEncoder class and setup
 * the image encoder
 */
static gboolean
gst_ce_imgenc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  GstCaps *allowed_caps = NULL;
  GstBuffer *codec_data = NULL;

  GstCeImgEnc *ce_imgenc = GST_CE_IMGENC (encoder);
  GstCeImgEncClass *klass =
      GST_CE_IMGENC_CLASS (G_OBJECT_GET_CLASS (ce_imgenc));
  GstCeImgEncPrivate *priv = ce_imgenc->priv;

  GST_DEBUG_OBJECT (ce_imgenc, "Extracting common image information");

  /* Prepare the input buffer descriptor */
  priv->frame_width = GST_VIDEO_INFO_WIDTH (&state->info);
  priv->frame_height = GST_VIDEO_INFO_HEIGHT (&state->info);
  priv->frame_pitch = GST_VIDEO_INFO_PLANE_STRIDE (&state->info, 0);
  priv->inbuf_desc.numBufs = GST_VIDEO_INFO_N_PLANES (&state->info);
  priv->video_format = GST_VIDEO_INFO_FORMAT (&state->info);

  GST_DEBUG_OBJECT (ce_imgenc, "input buffer format: width=%i, height=%i,"
      " pitch=%i", priv->frame_width, priv->frame_height, priv->frame_pitch);

  /* Configure codec with obtained information */
  if (!gst_ce_imgenc_configure_codec (ce_imgenc))
    goto fail_configure_codec;

  /* Some codecs support more than one format (JPEG encoder not), first auto-choose one */
  GST_DEBUG_OBJECT (ce_imgenc, "choosing an output format...");
  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  if (!allowed_caps) {
    GST_DEBUG_OBJECT (ce_imgenc, "... but no peer, using template caps");
    /* we need to copy because get_allowed_caps returns a ref, and
     * get_pad_template_caps doesn't */
    allowed_caps =
        gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  }
  GST_DEBUG_OBJECT (ce_imgenc, "chose caps %s",
      gst_caps_to_string (allowed_caps));

  /* If the codec supports more than one format, set custom source caps */
  if (klass->set_src_caps) {
    GST_DEBUG_OBJECT (ce_imgenc, "Use custom set src caps");
    if (!klass->set_src_caps (ce_imgenc, &allowed_caps, &codec_data))
      goto fail_set_src_caps;
  }

  /* Truncate to the first structure and fixate any unfixed fields */
  allowed_caps = gst_caps_fixate (allowed_caps);

  /* Store input state */
  if (priv->input_state)
    gst_video_codec_state_unref (priv->input_state);
  priv->input_state = gst_video_codec_state_ref (state);

  /* Set output state */
  if (priv->output_state)
    gst_video_codec_state_unref (priv->output_state);

  priv->output_state =
      gst_video_encoder_set_output_state (encoder, allowed_caps, state);
  if (!priv->output_state)
    goto fail_set_output_state;

  if (codec_data) {
    GST_DEBUG_OBJECT (ce_imgenc, "setting the codec data");
    priv->output_state->codec_data = codec_data;
  }

  return TRUE;

fail_configure_codec:
  GST_ERROR_OBJECT (ce_imgenc, "fail to configure codec");
  return FALSE;

fail_set_src_caps:
  GST_ERROR_OBJECT (ce_imgenc, "fail to set custom source caps");
  return FALSE;

fail_set_output_state:
  GST_ERROR_OBJECT (ce_imgenc, "fail to set output state");
  return FALSE;
}

/**
 * Suggest allocation parameters
 */
static gboolean
gst_ce_imgenc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  GstCeImgEnc *ce_imgenc = GST_CE_IMGENC (encoder);
  GstCeImgEncPrivate *priv = ce_imgenc->priv;
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
 * Setup the allocation parameters for allocating output buffers
 */
static gboolean
gst_ce_imgenc_decide_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  GstCeImgEnc *ce_imgenc = GST_CE_IMGENC (encoder);
  GstCeImgEncPrivate *priv = ce_imgenc->priv;
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstCaps *caps = NULL;

  GST_LOG_OBJECT (ce_imgenc, "decide allocation");
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

  GST_DEBUG_OBJECT (ce_imgenc,
      "allocation params flags=%d, align=%d, padding=%d, prefix=%d",
      params.flags, params.align, params.padding, params.prefix);
  priv->alloc_params = params;

  if (priv->output_state)
    caps = priv->output_state->caps;

  GST_DEBUG_OBJECT (ce_imgenc, "configuring output pool");
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

/**
 * Allocates a CMEM output buffer 
 */
static gboolean
gst_ce_imgenc_allocate_output_frame (GstCeImgEnc * ce_imgenc, GstBuffer ** buf)
{
  GstCeImgEncPrivate *priv = ce_imgenc->priv;

  /*Get allocator parameters from parent class */
  gst_video_encoder_get_allocator (GST_VIDEO_ENCODER (ce_imgenc), NULL,
      &priv->alloc_params);

  *buf = gst_buffer_new_allocate (priv->allocator, priv->outbuf_size,
      &priv->alloc_params);

  if (!*buf) {
    GST_DEBUG_OBJECT (ce_imgenc, "Can't alloc output buffer");
    return FALSE;
  }

  return TRUE;
}

/**
 *  Encodes the input data from GstVideoEncoder class
 */
static GstFlowReturn
gst_ce_imgenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstCeImgEnc *ce_imgenc = GST_CE_IMGENC (encoder);
  GstCeImgEncPrivate *priv = ce_imgenc->priv;
  GstCeImgEncClass *klass =
      GST_CE_IMGENC_CLASS (G_OBJECT_GET_CLASS (ce_imgenc));
  GstVideoInfo *info = &priv->input_state->info;
  GstVideoFrame vframe;
  GstMapInfo info_out;
  GstBuffer *outbuf = NULL;
  GstCeContigBufMeta *meta;
  IMGENC1_InArgs in_args;
  IMGENC1_OutArgs out_args;
  gint ret = IMGENC1_EFAIL;
  gint i = 0;
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

  for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (&vframe); i++) {
    priv->inbuf_desc.descs[i].buf =
        (XDAS_Int8 *) GST_VIDEO_FRAME_PLANE_DATA (&vframe, i);
  }

  current_pitch = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);

  gst_video_frame_unmap (&vframe);

  if (priv->frame_pitch != current_pitch) {
    priv->frame_pitch = current_pitch;
    switch (priv->video_format) {
      case GST_VIDEO_FORMAT_UYVY:
        ce_imgenc->codec_dyn_params->captureWidth = priv->frame_pitch / 2;
        break;
      case GST_VIDEO_FORMAT_NV12:
        ce_imgenc->codec_dyn_params->captureWidth = priv->frame_pitch;
        break;
      default:
        ce_imgenc->codec_dyn_params->captureWidth = 0;
    }
    if (!gst_ce_imgenc_set_dynamic_params (ce_imgenc))
      goto fail_set_buffer_stride;

    if (!gst_ce_imgenc_get_buffer_info (ce_imgenc))
      goto fail_set_buffer_stride;
  }

  /* Making sure the output buffer pool is configured */
  if (priv->first_buffer && gst_pad_check_reconfigure (encoder->srcpad)) {
    gst_video_encoder_negotiate (GST_VIDEO_ENCODER (encoder));
    priv->first_buffer = FALSE;
  }

  /* Allocate output buffer */
  if (gst_buffer_pool_acquire_buffer (GST_BUFFER_POOL_CAST (priv->outbuf_pool),
          &outbuf, NULL) != GST_FLOW_OK) {
    frame->output_buffer = NULL;
    gst_video_encoder_finish_frame (encoder, frame);
    goto fail_alloc;
  }

  if (!gst_buffer_map (outbuf, &info_out, GST_MAP_WRITE))
    goto fail_alloc;

  priv->outbuf_desc.descs[0].buf = (XDAS_Int8 *) info_out.data;

  /* Set output and input arguments for the encode process */
  in_args.size = sizeof (IIMGENC1_InArgs);
  out_args.size = sizeof (IMGENC1_OutArgs);

  /* Pre-encode process */
  if (klass->pre_process
      && !klass->pre_process (ce_imgenc, frame->input_buffer))
    goto fail_pre_encode;

  /* Encode process */
  ret = IMGENC1_process (ce_imgenc->codec_handle, &priv->inbuf_desc,
      &priv->outbuf_desc, &in_args, &out_args);

  if (IMGENC1_EOK != ret)
    goto fail_encode;

  GST_DEBUG_OBJECT (ce_imgenc,
      "encoded an output buffer of size %li at addr %p",
      out_args.bytesGenerated, priv->outbuf_desc.descs->buf);

  gst_buffer_unmap (outbuf, &info_out);
  gst_ce_slice_buffer_resize (GST_CE_SLICE_BUFFER_POOL_CAST (priv->outbuf_pool),
      outbuf, out_args.bytesGenerated);
  /* Post-encode process (JPEG encoder doesn't have a post-encode process) */
  if (klass->post_process && !klass->post_process (ce_imgenc, outbuf))
    goto fail_post_encode;

  GST_DEBUG_OBJECT (ce_imgenc, "frame encoded succesfully");

  frame->output_buffer = outbuf;

  return gst_video_encoder_finish_frame (encoder, frame);

fail_map:
  {
    GST_ERROR_OBJECT (encoder, "failed to map input buffer");
    return GST_FLOW_ERROR;
  }
fail_set_buffer_stride:
  {
    GST_ERROR_OBJECT (encoder, "failed to set buffer stride");
    return GST_FLOW_ERROR;
  }
fail_no_contiguous_buffer:
  {
    GST_ERROR_OBJECT (encoder, "input buffer should be contiguous");
    return GST_FLOW_ERROR;
  }
fail_alloc:
  {
    GST_INFO_OBJECT (ce_imgenc, "Failed to get output buffer, frame dropped");
    return GST_FLOW_OK;
  }
fail_pre_encode:
  {
    gst_buffer_unmap (outbuf, &info_out);
    GST_ERROR_OBJECT (ce_imgenc, "failed pre-encode process");
    return GST_FLOW_ERROR;
  }
fail_encode:
  {
    gst_buffer_unmap (outbuf, &info_out);
    GST_ERROR_OBJECT (ce_imgenc,
        "failed encode process with extended error: 0x%x",
        (unsigned int) out_args.extendedError);
    return GST_FLOW_ERROR;
  }
fail_post_encode:
  {
    GST_ERROR_OBJECT (ce_imgenc, "failed post-encode process");
    return GST_FLOW_ERROR;
  }
}

/**
 * Sets custom properties to image encoder
 */
static void
gst_ce_imgenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstCeImgEnc *ce_imgenc;
  GstCeImgEncClass *klass;
  IMGENC1_Params *params;
  IMGENC1_DynamicParams *dyn_params;
  IMGENC1_Status enc_status;

  gint ret = IMGENC1_EFAIL;

  /* Get a pointer of the right type */
  ce_imgenc = GST_CE_IMGENC (object);
  klass = GST_CE_IMGENC_CLASS (G_OBJECT_GET_CLASS (ce_imgenc));

  if ((!ce_imgenc->codec_params) || (!ce_imgenc->codec_dyn_params)) {
    GST_WARNING_OBJECT (ce_imgenc, "couldn't set property");
    return;
  }

  params = (IMGENC1_Params *) ce_imgenc->codec_params;
  dyn_params = (IMGENC1_DynamicParams *) ce_imgenc->codec_dyn_params;

  GST_OBJECT_LOCK (ce_imgenc);
  /* Check the argument id to see which argument we're setting */
  switch (prop_id) {
    case PROP_QUALITY_VALUE:
      dyn_params->qValue = g_value_get_int (value);
      GST_LOG_OBJECT (ce_imgenc,
          "setting quality value to %li", dyn_params->qValue);
      break;
    case PROP_NUM_OUT_BUFFERS:
      ce_imgenc->priv->num_out_buffers = g_value_get_int (value);
      GST_LOG_OBJECT (ce_imgenc,
          "setting number of output buffers to %d",
          ce_imgenc->priv->num_out_buffers);
      break;
    case PROP_MIN_SIZE_PERCENTAGE:
      ce_imgenc->priv->outbuf_size_percentage = g_value_get_int (value);
      GST_LOG_OBJECT (ce_imgenc,
          "setting min output buffer size percentage to %d",
          ce_imgenc->priv->outbuf_size_percentage);
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  if (ce_imgenc->codec_handle) {
    enc_status.size = sizeof (IMGENC1_Status);
    enc_status.data.buf = NULL;
    ret = IMGENC1_control (ce_imgenc->codec_handle, XDM_SETPARAMS,
        dyn_params, &enc_status);
    if (IMGENC1_EOK != ret)
      GST_WARNING_OBJECT (ce_imgenc, "failed to set dynamic parameters, "
          "status error %x, %d", (guint) enc_status.extendedError, ret);
  }

  GST_OBJECT_UNLOCK (ce_imgenc);
  return;
}

/**
 * Gets custom properties from image encoder
 */
static void
gst_ce_imgenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstCeImgEnc *ce_imgenc;
  GstCeImgEncClass *klass;
  IMGENC1_Params *params;
  IMGENC1_DynamicParams *dyn_params;

  /* It's not null if we got it, but it might not be ours */
  ce_imgenc = GST_CE_IMGENC (object);
  klass = GST_CE_IMGENC_CLASS (G_OBJECT_GET_CLASS (ce_imgenc));

  if ((!ce_imgenc->codec_params) || (!ce_imgenc->codec_dyn_params)) {
    GST_WARNING_OBJECT (ce_imgenc, "couldn't get property");
    return;
  }

  params = (IMGENC1_Params *) ce_imgenc->codec_params;
  dyn_params = (IMGENC1_DynamicParams *) ce_imgenc->codec_dyn_params;

  GST_OBJECT_LOCK (ce_imgenc);
  switch (prop_id) {
    case PROP_QUALITY_VALUE:
      g_value_set_int (value, dyn_params->qValue);
      break;
    case PROP_NUM_OUT_BUFFERS:
      g_value_set_int (value, ce_imgenc->priv->num_out_buffers);
      break;
    case PROP_MIN_SIZE_PERCENTAGE:
      g_value_set_int (value, ce_imgenc->priv->outbuf_size_percentage);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (ce_imgenc);
}

/**
 * Open Codec Engine
 */
static gboolean
gst_ce_imgenc_open (GstVideoEncoder * encoder)
{
  GstCeImgEnc *ce_imgenc = GST_CE_IMGENC (encoder);
  GstCeImgEncPrivate *priv = ce_imgenc->priv;

  GST_DEBUG_OBJECT (ce_imgenc, "opening %s Engine", CODEC_ENGINE);
  /* reset, load, and start DSP Engine */
  if ((priv->engine_handle =
          Engine_open ((Char *) CODEC_ENGINE, NULL, NULL)) == NULL)
    goto fail_engine_open;

  if (priv->allocator)
    gst_object_unref (priv->allocator);

  GST_DEBUG_OBJECT (ce_imgenc, "getting CMEM allocator");
  priv->allocator = gst_allocator_find ("ContiguousMemory");

  if (!priv->allocator)
    goto fail_no_allocator;

  GST_DEBUG_OBJECT (ce_imgenc, "creating slice buffer pool");

  if (!(priv->outbuf_pool = gst_ce_slice_buffer_pool_new ()))
    goto fail_pool;

  return TRUE;

  /* Errors */
fail_engine_open:
  {
    GST_ELEMENT_ERROR (ce_imgenc, STREAM, CODEC_NOT_FOUND, (NULL),
        ("failed to open codec engine \"%s\"", CODEC_ENGINE));
    return FALSE;
  }
fail_no_allocator:
  {
    GST_WARNING_OBJECT (ce_imgenc, "can't find the CMEM allocator");
    return FALSE;
  }
fail_pool:
  {
    GST_WARNING_OBJECT (ce_imgenc, "can't create slice buffer pool");
    return FALSE;
  }
  return TRUE;
}

/**
 * Close Codec Engine
 */
static gboolean
gst_ce_imgenc_close (GstVideoEncoder * encoder)
{
  GstCeImgEnc *ce_imgenc = GST_CE_IMGENC (encoder);
  GstCeImgEncPrivate *priv = ce_imgenc->priv;

  if (priv->engine_handle) {
    GST_DEBUG_OBJECT (ce_imgenc, "closing codec engine %p\n",
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

/**
 * Stop Codec Engine
 */
static gboolean
gst_ce_imgenc_stop (GstVideoEncoder * encoder)
{
  return gst_ce_imgenc_reset (encoder);
}

/**
 * Reset image encoder
 */
static gboolean
gst_ce_imgenc_reset (GstVideoEncoder * encoder)
{
  GstCeImgEnc *ce_imgenc = GST_CE_IMGENC (encoder);
  GstCeImgEncPrivate *priv = ce_imgenc->priv;
  GstCeImgEncClass *klass =
      GST_CE_IMGENC_CLASS (G_OBJECT_GET_CLASS (ce_imgenc));

  IMGENC1_Params *params = ce_imgenc->codec_params;
  IMGENC1_DynamicParams *dyn_params = ce_imgenc->codec_dyn_params;

  g_return_val_if_fail (params, FALSE);
  g_return_val_if_fail (dyn_params, FALSE);

  if (priv->input_state) {
    gst_video_codec_state_unref (priv->input_state);
    priv->input_state = NULL;
  }

  if (ce_imgenc->codec_handle) {
    IMGENC1_delete (ce_imgenc->codec_handle);
    ce_imgenc->codec_handle = NULL;
  }

  GST_OBJECT_LOCK (ce_imgenc);

  priv->num_out_buffers = PROP_NUM_OUT_BUFFERS_DEFAULT;
  priv->outbuf_size_percentage = PROP_MIN_SIZE_PERCENTAGE_DEFAULT;
  /* Set default values for codec static params */
  params->forceChromaFormat = XDM_YUV_420P;
  params->dataEndianness = XDM_BYTE;
  params->maxScans = XDM_DEFAULT;

  /* Set default values for codec dynamic params */
  dyn_params->qValue = PROP_QUALITY_VALUE_DEFAULT;
  dyn_params->numAU = XDM_DEFAULT;
  dyn_params->generateHeader = XDM_DEFAULT;

  GST_OBJECT_UNLOCK (ce_imgenc);

  /* Configure specific codec */
  if (klass->reset) {
    GST_DEBUG_OBJECT (ce_imgenc, "configuring codec");
    klass->reset (ce_imgenc);
  }

  return TRUE;
}

/**
 * Set current dynamic parameters 
 */
static gboolean
gst_ce_imgenc_set_dynamic_params (GstCeImgEnc * ce_imgenc)
{
  IMGENC1_Status enc_status;
  gint ret = IMGENC1_EFAIL;

  g_return_val_if_fail (ce_imgenc->codec_handle, FALSE);
  g_return_val_if_fail (ce_imgenc->codec_dyn_params, FALSE);

  enc_status.size = sizeof (IMGENC1_Status);
  enc_status.data.buf = NULL;

  ret = IMGENC1_control (ce_imgenc->codec_handle, XDM_SETPARAMS,
      ce_imgenc->codec_dyn_params, &enc_status);
  if (IMGENC1_EOK != ret) {
    GST_WARNING_OBJECT (ce_imgenc, "Failed to set dynamic parameters, "
        "status error %x, %d", (unsigned int) enc_status.extendedError, ret);
    return FALSE;
  }

  return TRUE;
}

/**
 * Get buffer information from video codec 
 */
static gboolean
gst_ce_imgenc_get_buffer_info (GstCeImgEnc * ce_imgenc)
{
  GstCeImgEncPrivate *priv = ce_imgenc->priv;
  IMGENC1_Status enc_status;
  gint i = 0;
  gint ret = IMGENC1_EFAIL;

  g_return_val_if_fail (ce_imgenc->codec_handle, FALSE);
  g_return_val_if_fail (ce_imgenc->codec_dyn_params, FALSE);

  enc_status.size = sizeof (IMGENC1_Status);
  enc_status.data.buf = NULL;

  ret = IMGENC1_control (ce_imgenc->codec_handle, XDM_GETBUFINFO,
      ce_imgenc->codec_dyn_params, &enc_status);
  if (IMGENC1_EOK != ret) {
    GST_ERROR_OBJECT (ce_imgenc, "failed to get buffer information, "
        "status error %x, %d", (guint) enc_status.extendedError, ret);
    return FALSE;
  }

  for (i = 0; i < enc_status.bufInfo.minNumInBufs; i++) {
    priv->inbuf_desc.descs[i].bufSize = enc_status.bufInfo.minInBufSize[i];
    GST_DEBUG_OBJECT (ce_imgenc, "size of input buffer [%d] = %li", i,
        priv->inbuf_desc.descs[i].bufSize);
  }

  priv->outbuf_size = enc_status.bufInfo.minOutBufSize[0];
  priv->outbuf_desc.numBufs = 1;
  priv->outbuf_desc.descs[0].bufSize = (XDAS_Int32) priv->outbuf_size;

  GST_DEBUG_OBJECT (ce_imgenc, "output buffer size = %d", priv->outbuf_size);

  return TRUE;
}
