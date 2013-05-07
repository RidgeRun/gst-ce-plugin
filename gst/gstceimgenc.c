/*
 * gstceimgenc.c
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

#include "gstce.h"
#include "gstceimgenc.h"
#include "gstceslicepool.h"

#include <ti/sdo/ce/osal/Memory.h>

enum
{
  PROP_0,
  PROP_QUALITY_VALUE,
  PROP_NUM_OUT_BUFFERS
};

#define PROP_QUALITY_VALUE_DEFAULT 75
#define PROP_NUM_OUT_BUFFERS_DEFAULT 3

#define GST_CEIMGENC_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_CEIMGENC, GstCEImgEncPrivate))

struct _GstCEImgEncPrivate
{
  gboolean first_buffer;

  /* Basic properties */
  gint32 frame_width;
  gint32 frame_height;
  gint32 frame_pitch;

  gint32 outbuf_size;
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
static gboolean gst_ceimgenc_open (GstVideoEncoder * encoder);
static gboolean gst_ceimgenc_close (GstVideoEncoder * encoder);
static gboolean gst_ceimgenc_stop (GstVideoEncoder * encoder);
static gboolean gst_ceimgenc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static gboolean gst_ceimgenc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);
static gboolean gst_ceimgenc_decide_allocation (GstVideoEncoder * encoder,
    GstQuery * query);
static GstFlowReturn gst_ceimgenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);

static void gst_ceimgenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ceimgenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_ceimgenc_reset (GstVideoEncoder * encoder);
static void gst_ceimgenc_finalize (GObject * object);
static gboolean gst_ceimgenc_set_dynamic_params (GstCEImgEnc * ceimgenc);
static gboolean gst_ceimgenc_get_buffer_info (GstCEImgEnc * ceimgenc);

#define gst_ceimgenc_parent_class parent_class
G_DEFINE_TYPE (GstCEImgEnc, gst_ceimgenc, GST_TYPE_VIDEO_ENCODER);

/**
 * Image encoder class initialization function
 */
static void
gst_ceimgenc_class_init (GstCEImgEncClass * klass)
{
  GObjectClass *gobject_class;
  GstVideoEncoderClass *venc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  venc_class = GST_VIDEO_ENCODER_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstCEImgEncPrivate));

  gobject_class->set_property = gst_ceimgenc_set_property;
  gobject_class->get_property = gst_ceimgenc_get_property;
  gobject_class->finalize = gst_ceimgenc_finalize;

  /* Initialization of the image encoder properties */
  g_object_class_install_property (gobject_class, PROP_QUALITY_VALUE,
      g_param_spec_int ("qValue", "Quality value",
          "Quality factor for encoder", 2, 97,
          PROP_QUALITY_VALUE_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_NUM_OUT_BUFFERS,
      g_param_spec_int ("num-out-buffers",
          "Number of output buffers",
          "Number of buffers to be used in the output buffer pool",
          2, G_MAXINT32, PROP_NUM_OUT_BUFFERS_DEFAULT, G_PARAM_READWRITE));

  venc_class->open = GST_DEBUG_FUNCPTR (gst_ceimgenc_open);
  venc_class->close = GST_DEBUG_FUNCPTR (gst_ceimgenc_close);
  venc_class->stop = GST_DEBUG_FUNCPTR (gst_ceimgenc_stop);
  venc_class->handle_frame = GST_DEBUG_FUNCPTR (gst_ceimgenc_handle_frame);
  venc_class->set_format = GST_DEBUG_FUNCPTR (gst_ceimgenc_set_format);
  venc_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_ceimgenc_propose_allocation);
  venc_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ceimgenc_decide_allocation);
}

/**
 * Image encoder initialization function
 */
static void
gst_ceimgenc_init (GstCEImgEnc * ceimgenc)
{
  GstCEImgEncPrivate *priv;

  GST_DEBUG_OBJECT (ceimgenc, "initialize encoder");

  priv = ceimgenc->priv = GST_CEIMGENC_GET_PRIVATE (ceimgenc);

  /* Allocate the codec parameters */
  if (!ceimgenc->codec_params) {
    GST_DEBUG_OBJECT (ceimgenc, "allocating codec params");
    ceimgenc->codec_params = g_malloc0 (sizeof (IMGENC1_Params));
    if (ceimgenc->codec_params == NULL) {
      GST_WARNING_OBJECT (ceimgenc, "failed to allocate IMGENC1_Params");
      return;
    }
    ceimgenc->codec_params->size = sizeof (IMGENC1_Params);
  }

  /* Allocate the codec dynamic parameters */
  if (!ceimgenc->codec_dyn_params) {
    GST_DEBUG_OBJECT (ceimgenc, "allocating codec dynamic params");
    ceimgenc->codec_dyn_params = g_malloc0 (sizeof (IMGENC1_DynamicParams));
    if (ceimgenc->codec_dyn_params == NULL) {
      GST_WARNING_OBJECT (ceimgenc, "failed to allocate IMGENC1_DynamicParams");
      return;
    }
    ceimgenc->codec_dyn_params->size = sizeof (IMGENC1_DynamicParams);
  }

  priv->first_buffer = TRUE;
  priv->engine_handle = NULL;
  priv->allocator = NULL;

  gst_ceimgenc_reset ((GstVideoEncoder *) ceimgenc);
}

/**
 * Image encoder class finalization function
 */
static void
gst_ceimgenc_finalize (GObject * object)
{
  GstCEImgEnc *ceimgenc = GST_CEIMGENC (object);

  /* Free the codec parameters */
  if (ceimgenc->codec_params) {
    g_free (ceimgenc->codec_params);
    ceimgenc->codec_params = NULL;
  }

  /* Free the codec dynamic parameters */
  if (ceimgenc->codec_dyn_params) {
    g_free (ceimgenc->codec_dyn_params);
    ceimgenc->codec_dyn_params = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * Based on the negotiated format, creates and initializes the 
 * codec instance
 */
static gboolean
gst_ceimgenc_configure_codec (GstCEImgEnc * ceimgenc)
{
  GstCEImgEncClass *klass;
  GstCEImgEncPrivate *priv;
  IMGENC1_Status enc_status;
  IMGENC1_Params *params;
  IMGENC1_DynamicParams *dyn_params;

  klass = GST_CEIMGENC_CLASS (G_OBJECT_GET_CLASS (ceimgenc));
  priv = ceimgenc->priv;
  params = ceimgenc->codec_params;
  dyn_params = ceimgenc->codec_dyn_params;

  g_return_val_if_fail (params, FALSE);
  g_return_val_if_fail (dyn_params, FALSE);
  g_return_val_if_fail (klass->codec_name, FALSE);

  GST_OBJECT_LOCK (ceimgenc);

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
      GST_ELEMENT_ERROR (ceimgenc, STREAM, NOT_IMPLEMENTED,
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
  if (ceimgenc->codec_handle) {
    GST_DEBUG_OBJECT (ceimgenc, "Closing old codec session");
    IMGENC1_delete (ceimgenc->codec_handle);
  }

  GST_DEBUG_OBJECT (ceimgenc, "Create the codec handle");
  ceimgenc->codec_handle = IMGENC1_create (priv->engine_handle,
      (Char *) klass->codec_name, params);
  if (!ceimgenc->codec_handle)
    goto fail_open_codec;

  /* Set codec dynamic parameters */
  enc_status.size = sizeof (IMGENC1_Status);
  enc_status.data.buf = NULL;

  GST_DEBUG_OBJECT (ceimgenc, "Set codec dynamic parameters");
  if (!gst_ceimgenc_set_dynamic_params (ceimgenc))
    goto fail_out;

  if (!gst_ceimgenc_get_buffer_info (ceimgenc))
    goto fail_out;

  GST_OBJECT_UNLOCK (ceimgenc);

  return TRUE;

fail_open_codec:
  {
    GST_ERROR_OBJECT (ceimgenc, "Failed to open codec %s", klass->codec_name);
    GST_OBJECT_UNLOCK (ceimgenc);
    return FALSE;
  }
fail_out:
  {
    IMGENC1_delete (ceimgenc->codec_handle);
    ceimgenc->codec_handle = NULL;
    GST_OBJECT_UNLOCK (ceimgenc);
    return FALSE;
  }
}

/**
 * Gets the format of input video data from GstVideoEncoder class and setup
 * the image encoder
 */
static gboolean
gst_ceimgenc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  GstCaps *allowed_caps;
  GstBuffer *codec_data = NULL;

  GstCEImgEnc *ceimgenc = GST_CEIMGENC (encoder);
  GstCEImgEncClass *klass = GST_CEIMGENC_CLASS (G_OBJECT_GET_CLASS (ceimgenc));
  GstCEImgEncPrivate *priv = ceimgenc->priv;

  GST_DEBUG_OBJECT (ceimgenc, "Extracting common image information");

  /* Prepare the input buffer descriptor */
  priv->frame_width = GST_VIDEO_INFO_WIDTH (&state->info);
  priv->frame_height = GST_VIDEO_INFO_HEIGHT (&state->info);
  priv->frame_pitch = GST_VIDEO_INFO_PLANE_STRIDE (&state->info, 0);
  priv->inbuf_desc.numBufs = GST_VIDEO_INFO_N_PLANES (&state->info);
  priv->video_format = GST_VIDEO_INFO_FORMAT (&state->info);

  GST_DEBUG_OBJECT (ceimgenc, "input buffer format: width=%i, height=%i,"
      " pitch=%i", priv->frame_width, priv->frame_height, priv->frame_pitch);

  /* Configure codec with obtained information */
  if (!gst_ceimgenc_configure_codec (ceimgenc))
    goto fail_set_caps;

  /* Some codecs support more than one format (JPEG encoder not), first auto-choose one */
  GST_DEBUG_OBJECT (ceimgenc, "choosing an output format...");
  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  if (!allowed_caps) {
    GST_DEBUG_OBJECT (ceimgenc, "... but no peer, using template caps");
    /* we need to copy because get_allowed_caps returns a ref, and
     * get_pad_template_caps doesn't */
    allowed_caps =
        gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  }
  GST_DEBUG_OBJECT (ceimgenc, "chose caps %" GST_PTR_FORMAT, allowed_caps);

  /* If the codec supports more than one format, set custom source caps */
  if (klass->set_src_caps) {
    GST_DEBUG_OBJECT (ceimgenc, "Use custom set src caps");
    if (!klass->set_src_caps (ceimgenc, &allowed_caps, &codec_data))
      goto fail_set_caps;
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
    goto fail_set_caps;

  if (codec_data) {
    GST_DEBUG_OBJECT (ceimgenc, "setting the codec data");
    priv->output_state->codec_data = codec_data;
  }

  return TRUE;

fail_set_caps:
  GST_ERROR_OBJECT (ceimgenc, "couldn't set image format");
  return FALSE;
}

/**
 * Suggest allocation parameters
 */
static gboolean
gst_ceimgenc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  GstCEImgEnc *ceimgenc = GST_CEIMGENC (encoder);
  GstCEImgEncPrivate *priv = ceimgenc->priv;
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
gst_ceimgenc_decide_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  GstCEImgEnc *ceimgenc = GST_CEIMGENC (encoder);
  GstCEImgEncPrivate *priv = ceimgenc->priv;
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstCaps *caps;

  GST_LOG_OBJECT (ceimgenc, "decide allocation");
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

  GST_DEBUG_OBJECT (ceimgenc, "allocation params %d, %d, %d, %d", params.flags,
      params.align, params.padding, params.prefix);
  priv->alloc_params = params;

  if (priv->output_state)
    caps = priv->output_state->caps;

  GST_DEBUG_OBJECT (ceimgenc, "configuring output pool");
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, priv->outbuf_size, 1,
      priv->num_out_buffers);
  gst_buffer_pool_config_set_allocator (config, priv->allocator,
      &priv->alloc_params);
  gst_buffer_pool_set_config (GST_BUFFER_POOL_CAST (pool), config);
  gst_buffer_pool_set_active (GST_BUFFER_POOL_CAST (pool), TRUE);

  return TRUE;
}

/**
 * Allocates a CMEM output buffer 
 */
static gboolean
gst_ceimgenc_allocate_output_frame (GstCEImgEnc * ceimgenc, GstBuffer ** buf)
{
  GstCEImgEncPrivate *priv = ceimgenc->priv;

  /*Get allocator parameters from parent class */
  gst_video_encoder_get_allocator (GST_VIDEO_ENCODER (ceimgenc), NULL,
      &priv->alloc_params);

  *buf = gst_buffer_new_allocate (priv->allocator, priv->outbuf_size,
      &priv->alloc_params);

  if (!*buf) {
    GST_DEBUG_OBJECT (ceimgenc, "Can't alloc output buffer");
    return FALSE;
  }

  return TRUE;
}

/**
 *  Encodes the input data from GstVideoEncoder class
 */
static GstFlowReturn
gst_ceimgenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstCEImgEnc *ceimgenc = GST_CEIMGENC (encoder);
  GstCEImgEncPrivate *priv = ceimgenc->priv;
  GstCEImgEncClass *klass = GST_CEIMGENC_CLASS (G_OBJECT_GET_CLASS (ceimgenc));
  GstVideoInfo *info = &priv->input_state->info;
  GstVideoFrame vframe;
  GstMapInfo info_out;
  GstBuffer *outbuf = NULL;
  GstCEContigBufMeta *meta;
  IMGENC1_InArgs in_args;
  IMGENC1_OutArgs out_args;
  gint ret = 0;
  gint i;
  gint current_pitch;

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
        ceimgenc->codec_dyn_params->captureWidth = priv->frame_pitch / 2;
        break;
      case GST_VIDEO_FORMAT_NV12:
        ceimgenc->codec_dyn_params->captureWidth = priv->frame_pitch;
        break;
      default:
        ceimgenc->codec_dyn_params->captureWidth = 0;
    }
    if (!gst_ceimgenc_set_dynamic_params (ceimgenc))
      goto fail_set_buffer_stride;

    if (!gst_ceimgenc_get_buffer_info (ceimgenc))
      goto fail_set_buffer_stride;
  }

  /* Making sure the output buffer pool is configured */
  if (priv->first_buffer && gst_pad_check_reconfigure (encoder->srcpad)) {
    gst_video_encoder_negotiate (GST_VIDEO_ENCODER (encoder));
    priv->first_buffer = FALSE;
  }

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
  if (gst_buffer_pool_acquire_buffer (GST_BUFFER_POOL_CAST (priv->outbuf_pool),
          &outbuf, NULL) != GST_FLOW_OK)
    goto fail_alloc;
  if (!gst_buffer_map (outbuf, &info_out, GST_MAP_WRITE))
    goto fail_alloc;

  priv->outbuf_desc.descs[0].buf = (XDAS_Int8 *) info_out.data;

  /* Set output and input arguments for the encode process */
  in_args.size = sizeof (IIMGENC1_InArgs);
  out_args.size = sizeof (IMGENC1_OutArgs);

  /* Pre-encode process */
  if (klass->pre_process && !klass->pre_process (ceimgenc, frame->input_buffer))
    goto fail_pre_encode;

  /* Encode process */
  ret = IMGENC1_process (ceimgenc->codec_handle, &priv->inbuf_desc,
      &priv->outbuf_desc, &in_args, &out_args);

  if (ret != IMGENC1_EOK)
    goto fail_encode;

  GST_WARNING_OBJECT (ceimgenc,
      "encoded an output buffer of size %li at addr %p",
      out_args.bytesGenerated, priv->outbuf_desc.descs->buf);

  gst_buffer_unmap (outbuf, &info_out);
  gst_ce_slice_buffer_resize (GST_CE_SLICE_BUFFER_POOL_CAST (priv->outbuf_pool),
      outbuf, out_args.bytesGenerated);
  /* Post-encode process (JPEG encoder doesn't have a post-encode process) */
  if (klass->post_process && !klass->post_process (ceimgenc, outbuf))
    goto fail_post_encode;

  GST_DEBUG_OBJECT (ceimgenc, "frame encoded succesfully");

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
    GST_ERROR_OBJECT (ceimgenc, "failed to get output buffer");
    return GST_FLOW_ERROR;
  }
fail_pre_encode:
  {
    gst_buffer_unmap (outbuf, &info_out);
    GST_ERROR_OBJECT (ceimgenc, "failed pre-encode process");
    return GST_FLOW_ERROR;
  }
fail_encode:
  {
    gst_buffer_unmap (outbuf, &info_out);
    GST_ERROR_OBJECT (ceimgenc,
        "failed encode process with extended error: 0x%x",
        (unsigned int) out_args.extendedError);
    return GST_FLOW_ERROR;
  }
fail_post_encode:
  {
    GST_ERROR_OBJECT (ceimgenc, "failed post-encode process");
    return GST_FLOW_ERROR;
  }
}

/**
 * Sets custom properties to image encoder
 */
static void
gst_ceimgenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstCEImgEnc *ceimgenc;
  GstCEImgEncClass *klass;
  IMGENC1_Params *params;
  IMGENC1_DynamicParams *dyn_params;
  IMGENC1_Status enc_status;

  gint ret;

  /* Get a pointer of the right type */
  ceimgenc = GST_CEIMGENC (object);
  klass = GST_CEIMGENC_CLASS (G_OBJECT_GET_CLASS (ceimgenc));

  if ((!ceimgenc->codec_params) || (!ceimgenc->codec_dyn_params)) {
    GST_WARNING_OBJECT (ceimgenc, "couldn't set property");
    return;
  }

  params = (IMGENC1_Params *) ceimgenc->codec_params;
  dyn_params = (IMGENC1_DynamicParams *) ceimgenc->codec_dyn_params;

  GST_OBJECT_LOCK (ceimgenc);
  /* Check the argument id to see which argument we're setting */
  switch (prop_id) {
    case PROP_QUALITY_VALUE:
      dyn_params->qValue = g_value_get_int (value);
      GST_LOG_OBJECT (ceimgenc,
          "setting quality value to %li", dyn_params->qValue);
      break;
    case PROP_NUM_OUT_BUFFERS:
      ceimgenc->priv->num_out_buffers = g_value_get_int (value);
      GST_LOG_OBJECT (ceimgenc,
          "setting number of output buffers to %d",
          ceimgenc->priv->num_out_buffers);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  if (ceimgenc->codec_handle) {
    enc_status.size = sizeof (IMGENC1_Status);
    enc_status.data.buf = NULL;
    ret = IMGENC1_control (ceimgenc->codec_handle, XDM_SETPARAMS,
        dyn_params, &enc_status);
    if (ret != IMGENC1_EOK)
      GST_WARNING_OBJECT (ceimgenc, "failed to set dynamic parameters, "
          "status error %x, %d", (guint) enc_status.extendedError, ret);
  }

  GST_OBJECT_UNLOCK (ceimgenc);
  return;
}

/**
 * Gets custom properties from image encoder
 */
static void
gst_ceimgenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstCEImgEnc *ceimgenc;
  GstCEImgEncClass *klass;
  IMGENC1_Params *params;
  IMGENC1_DynamicParams *dyn_params;

  /* It's not null if we got it, but it might not be ours */
  ceimgenc = GST_CEIMGENC (object);
  klass = GST_CEIMGENC_CLASS (G_OBJECT_GET_CLASS (ceimgenc));

  if ((!ceimgenc->codec_params) || (!ceimgenc->codec_dyn_params)) {
    GST_WARNING_OBJECT (ceimgenc, "couldn't get property");
    return;
  }

  params = (IMGENC1_Params *) ceimgenc->codec_params;
  dyn_params = (IMGENC1_DynamicParams *) ceimgenc->codec_dyn_params;

  GST_OBJECT_LOCK (ceimgenc);
  switch (prop_id) {
    case PROP_QUALITY_VALUE:
      g_value_set_int (value, dyn_params->qValue);
      break;
    case PROP_NUM_OUT_BUFFERS:
      g_value_set_int (value, ceimgenc->priv->num_out_buffers);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (ceimgenc);
}

/**
 * Open Codec Engine
 */
static gboolean
gst_ceimgenc_open (GstVideoEncoder * encoder)
{
  GstCEImgEnc *ceimgenc = GST_CEIMGENC (encoder);
  GstCEImgEncPrivate *priv = ceimgenc->priv;

  GST_DEBUG_OBJECT (ceimgenc, "opening %s Engine", CODEC_ENGINE);
  /* reset, load, and start DSP Engine */
  if ((priv->engine_handle =
          Engine_open ((Char *) CODEC_ENGINE, NULL, NULL)) == NULL)
    goto fail_engine_open;

  if (priv->allocator)
    gst_object_unref (priv->allocator);

  GST_DEBUG_OBJECT (ceimgenc, "getting CMEM allocator");
  priv->allocator = gst_allocator_find ("ContiguousMemory");

  if (!priv->allocator)
    goto fail_no_allocator;

  GST_DEBUG_OBJECT (ceimgenc, "creating slice buffer pool");

  if (!(priv->outbuf_pool = gst_ce_slice_buffer_pool_new ()))
    goto fail_pool;

  return TRUE;

  /* Errors */
fail_engine_open:
  {
    GST_ELEMENT_ERROR (ceimgenc, STREAM, CODEC_NOT_FOUND, (NULL),
        ("failed to open codec engine \"%s\"", CODEC_ENGINE));
    return FALSE;
  }
fail_no_allocator:
  {
    GST_WARNING_OBJECT (ceimgenc, "can't find the CMEM allocator");
    return FALSE;
  }
fail_pool:
  {
    GST_WARNING_OBJECT (ceimgenc, "can't create slice buffer pool");
    return FALSE;
  }
  return TRUE;
}

/**
 * Close Codec Engine
 */
static gboolean
gst_ceimgenc_close (GstVideoEncoder * encoder)
{
  GstCEImgEnc *ceimgenc = GST_CEIMGENC (encoder);
  GstCEImgEncPrivate *priv = ceimgenc->priv;

  if (priv->engine_handle) {
    GST_DEBUG_OBJECT (ceimgenc, "closing codec engine %p\n",
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
gst_ceimgenc_stop (GstVideoEncoder * encoder)
{
  return gst_ceimgenc_reset (encoder);
}

/**
 * Reset image encoder
 */
static gboolean
gst_ceimgenc_reset (GstVideoEncoder * encoder)
{
  GstCEImgEnc *ceimgenc = GST_CEIMGENC (encoder);
  GstCEImgEncPrivate *priv = ceimgenc->priv;
  GstCEImgEncClass *klass = GST_CEIMGENC_CLASS (G_OBJECT_GET_CLASS (ceimgenc));

  IMGENC1_Params *params = ceimgenc->codec_params;
  IMGENC1_DynamicParams *dyn_params = ceimgenc->codec_dyn_params;

  g_return_val_if_fail (params, FALSE);
  g_return_val_if_fail (dyn_params, FALSE);

  if (priv->input_state) {
    gst_video_codec_state_unref (priv->input_state);
    priv->input_state = NULL;
  }

  if (ceimgenc->codec_handle) {
    IMGENC1_delete (ceimgenc->codec_handle);
    ceimgenc->codec_handle = NULL;
  }

  GST_OBJECT_LOCK (ceimgenc);

  priv->num_out_buffers = PROP_NUM_OUT_BUFFERS;

  /* Set default values for codec static params */
  params->forceChromaFormat = XDM_YUV_420P;
  params->dataEndianness = XDM_BYTE;
  params->maxScans = XDM_DEFAULT;

  /* Set default values for codec dynamic params */
  dyn_params->qValue = PROP_QUALITY_VALUE_DEFAULT;
  dyn_params->numAU = XDM_DEFAULT;
  dyn_params->generateHeader = XDM_DEFAULT;

  GST_OBJECT_UNLOCK (ceimgenc);

  /* Configure specific codec */
  if (klass->reset) {
    GST_DEBUG_OBJECT (ceimgenc, "configuring codec");
    klass->reset (ceimgenc);
  }

  return TRUE;
}

/**
 * Set current dynamic parameters 
 */
static gboolean
gst_ceimgenc_set_dynamic_params (GstCEImgEnc * ceimgenc)
{
  IMGENC1_Status enc_status;
  gint ret;

  g_return_val_if_fail (ceimgenc->codec_handle, FALSE);
  g_return_val_if_fail (ceimgenc->codec_dyn_params, FALSE);

  enc_status.size = sizeof (IMGENC1_Status);
  enc_status.data.buf = NULL;

  ret = IMGENC1_control (ceimgenc->codec_handle, XDM_SETPARAMS,
      ceimgenc->codec_dyn_params, &enc_status);
  if (ret != IMGENC1_EOK) {
    GST_WARNING_OBJECT (ceimgenc, "Failed to set dynamic parameters, "
        "status error %x, %d", (unsigned int) enc_status.extendedError, ret);
    return FALSE;
  }

  return TRUE;
}

/**
 * Get buffer information from video codec 
 */
static gboolean
gst_ceimgenc_get_buffer_info (GstCEImgEnc * ceimgenc)
{
  GstCEImgEncPrivate *priv = ceimgenc->priv;
  IMGENC1_Status enc_status;
  gint i, ret;

  g_return_val_if_fail (ceimgenc->codec_handle, FALSE);
  g_return_val_if_fail (ceimgenc->codec_dyn_params, FALSE);

  enc_status.size = sizeof (IMGENC1_Status);
  enc_status.data.buf = NULL;

  ret = IMGENC1_control (ceimgenc->codec_handle, XDM_GETBUFINFO,
      ceimgenc->codec_dyn_params, &enc_status);
  if (ret != IMGENC1_EOK) {
    GST_ERROR_OBJECT (ceimgenc, "failed to get buffer information, "
        "status error %x, %d", (guint) enc_status.extendedError, ret);
    return FALSE;
  }

  for (i = 0; i < enc_status.bufInfo.minNumInBufs; i++) {
    priv->inbuf_desc.descs[i].bufSize = enc_status.bufInfo.minInBufSize[i];
    GST_DEBUG_OBJECT (ceimgenc, "size of input buffer [%d] = %li", i,
        priv->inbuf_desc.descs[i].bufSize);
  }

  priv->outbuf_size = enc_status.bufInfo.minOutBufSize[0];
  priv->outbuf_desc.numBufs = 1;
  priv->outbuf_desc.descs[0].bufSize = (XDAS_Int32) priv->outbuf_size;

  GST_DEBUG_OBJECT (ceimgenc, "output buffer size = %d", priv->outbuf_size);

  return TRUE;
}
