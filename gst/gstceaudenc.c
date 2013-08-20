/*
 * gstceaudenc.c
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
 * SECTION:gstceaudenc
 * @short_description: Base class for Codec Engine audio encoders
 * @see_also:
 *
 * This base class is for audio encoders turning raw audio into
 * encoded audio data using TI codecs with the AUDENC1 audio encoding
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

#include "gstce.h"
#include "gstceaudenc.h"
#include "gstceslicepool.h"
#include <ti/sdo/ce/osal/Memory.h>
#include <ittiam/codecs/aaclc_enc/ieaacplusenc.h>

enum
{
  PROP_0,
  PROP_BITRATE,
  PROP_MAX_BITRATE,
  PROP_NUM_OUT_BUFFERS
};

#define PROP_BITRATE_DEFAULT          128000
#define PROP_MAX_BITRATE_DEFAULT      128000
#define PROP_NUM_OUT_BUFFERS_DEFAULT       3

#define SAMPLE_RATE_DEFAULT            48000
#define INPUT_BITS_PER_SAMPLE_DEFAULT     16

#define GST_CEAUDENC_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_CEAUDENC, GstCeAudEncPrivate))

struct _GstCeAudEncPrivate
{
  gint32 outbuf_size;
  GstBufferPool *outbuf_pool;
  gint num_out_buffers;
  /* Audio Information */
  gint channels;
  gint rate;
  gint width;
  gint depth;
  gint bpf;
  gint min_samples;
  gint max_samples;
  gint samples;

  /* Handle to the CMEM allocator */
  GstAllocator *allocator;
  GstAllocationParams alloc_params;
  GstBuffer *inbuf;
  
  /* Codec Data */
  Engine_Handle engine_handle;
  XDM1_BufDesc inbuf_desc;
  XDM1_BufDesc outbuf_desc;
};

/* A number of function prototypes are given so we can refer to them later. */
static gboolean gst_ce_audenc_open (GstAudioEncoder * encoder);
static gboolean gst_ce_audenc_close (GstAudioEncoder * encoder);
static gboolean gst_ce_audenc_stop (GstAudioEncoder * encoder);
static gboolean gst_ce_audenc_set_format (GstAudioEncoder * encoder,
    GstAudioInfo * state);
static gboolean gst_ce_audenc_decide_allocation (GstAudioEncoder * encoder,
    GstQuery * query);
static GstFlowReturn gst_ce_audenc_handle_frame (GstAudioEncoder * encoder,
    GstBuffer * buffer);

static void gst_ce_audenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ce_audenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_ce_audenc_finalize (GObject * object);

static gboolean gst_ce_audenc_reset (GstAudioEncoder * encoder);
static gboolean gst_ce_audenc_set_dynamic_params (GstCeAudEnc * ceaudenc);
static gboolean gst_ce_audenc_get_buffer_info (GstCeAudEnc * ceaudenc);
static gboolean gst_ce_audenc_allocate_frame (GstCeAudEnc * ceaudenc,
    GstBuffer ** buf, guint size);
#define gst_ce_audenc_parent_class parent_class
G_DEFINE_TYPE (GstCeAudEnc, gst_ce_audenc, GST_TYPE_AUDIO_ENCODER);

static void
gst_ce_audenc_class_init (GstCeAudEncClass * klass)
{
  GObjectClass *gobject_class;
  GstAudioEncoderClass *aenc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  aenc_class = GST_AUDIO_ENCODER_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstCeAudEncPrivate));

  /* 
   * Not using GST_DEBUG_FUNCPTR with GObject
   * virtual functions because no one will use them
   */
  gobject_class->set_property = gst_ce_audenc_set_property;
  gobject_class->get_property = gst_ce_audenc_get_property;
  gobject_class->finalize = gst_ce_audenc_finalize;

  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_int ("bitrate",
          "Bit rate",
          "Average bit rate in bps",
          0, G_MAXINT32, PROP_BITRATE_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_MAX_BITRATE,
      g_param_spec_int ("max-bitrate",
          "Max bitrate for VBR encoding",
          "Max bitrate for VBR encoding",
          0, G_MAXINT32, PROP_MAX_BITRATE_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_NUM_OUT_BUFFERS,
      g_param_spec_int ("num-out-buffers",
          "Number of output buffers",
          "Number of buffers to be used in the output buffer pool, "
          "each buffer contains the maximum amount of samples supported by the audio codec",
          3, G_MAXINT32, PROP_NUM_OUT_BUFFERS_DEFAULT, G_PARAM_READWRITE));

  aenc_class->open = GST_DEBUG_FUNCPTR (gst_ce_audenc_open);
  aenc_class->close = GST_DEBUG_FUNCPTR (gst_ce_audenc_close);
  aenc_class->stop = GST_DEBUG_FUNCPTR (gst_ce_audenc_stop);
  aenc_class->handle_frame = GST_DEBUG_FUNCPTR (gst_ce_audenc_handle_frame);
  aenc_class->set_format = GST_DEBUG_FUNCPTR (gst_ce_audenc_set_format);
  aenc_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ce_audenc_decide_allocation);

}

static void
gst_ce_audenc_init (GstCeAudEnc * ceaudenc)
{
  GstCeAudEncPrivate *priv;

  GST_DEBUG_OBJECT (ceaudenc, "initialize encoder");

  priv = ceaudenc->priv = GST_CEAUDENC_GET_PRIVATE (ceaudenc);

  /* Allocate the codec params */
  if (!ceaudenc->codec_params) {
    GST_DEBUG_OBJECT (ceaudenc, "allocating codec params");
    ceaudenc->codec_params = g_malloc0 (sizeof (AUDENC1_Params));
    if (!ceaudenc->codec_params) {
      GST_ELEMENT_ERROR (ceaudenc, RESOURCE, NO_SPACE_LEFT,
          (("failed to allocate AUDENC1_Params")), (NULL));

      return;
    }
    ceaudenc->codec_params->size = sizeof (AUDENC1_Params);
  }

  if (!ceaudenc->codec_dyn_params) {
    GST_DEBUG_OBJECT (ceaudenc, "allocating codec dynamic params");
    ceaudenc->codec_dyn_params = g_malloc0 (sizeof (AUDENC1_DynamicParams));
    if (!ceaudenc->codec_dyn_params) {
      GST_ELEMENT_ERROR (ceaudenc, RESOURCE, NO_SPACE_LEFT,
          (("failed to allocate AUDENC1_DynamicParams")), (NULL));
      return;
    }
    ceaudenc->codec_dyn_params->size = sizeof (AUDENC1_DynamicParams);
  }

  priv->engine_handle = NULL;
  priv->allocator = NULL;

  gst_ce_audenc_reset ((GstAudioEncoder *) ceaudenc);
}

static void
gst_ce_audenc_finalize (GObject * object)
{
  GstCeAudEnc *ceaudenc = GST_CEAUDENC (object);

  /* Free the allocated resources */
  if (ceaudenc->codec_params) {
    g_free (ceaudenc->codec_params);
    ceaudenc->codec_params = NULL;
  }

  if (ceaudenc->codec_dyn_params) {
    g_free (ceaudenc->codec_dyn_params);
    ceaudenc->codec_dyn_params = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_ce_audenc_configure_codec:
 * Based on the negotiated format, create and initialize the 
 * codec instance */
static gboolean
gst_ce_audenc_configure_codec (GstCeAudEnc * ceaudenc)
{
  GstCeAudEncClass *klass;
  GstCeAudEncPrivate *priv;
  AUDENC1_Params *params;
  AUDENC1_DynamicParams *dyn_params;

  klass = GST_CEAUDENC_CLASS (G_OBJECT_GET_CLASS (ceaudenc));
  priv = ceaudenc->priv;
  params = ceaudenc->codec_params;
  dyn_params = ceaudenc->codec_dyn_params;

  g_return_val_if_fail (params, FALSE);
  g_return_val_if_fail (dyn_params, FALSE);
  g_return_val_if_fail (klass->codec_name, FALSE);

  GST_OBJECT_LOCK (ceaudenc);

  switch (priv->channels) {
    case 1:
      params->channelMode = dyn_params->channelMode = IAUDIO_1_0;
      break;
    case 2:
      params->channelMode = dyn_params->channelMode = IAUDIO_2_0;
      break;
    default:
      goto fail_channels;
  }

  params->sampleRate = dyn_params->sampleRate = priv->rate;
  params->inputBitsPerSample = priv->width;

  if (ceaudenc->codec_handle) {
    GST_DEBUG_OBJECT (ceaudenc, "closing old codec session");
    AUDENC1_delete (ceaudenc->codec_handle);
  }

  GST_DEBUG_OBJECT (ceaudenc, "create the codec handle");
  ceaudenc->codec_handle = AUDENC1_create (priv->engine_handle,
      (Char *) klass->codec_name, params);
  if (!ceaudenc->codec_handle)
    goto fail_open_codec;

  GST_DEBUG_OBJECT (ceaudenc, "set codec dynamic parameters");
  if (!gst_ce_audenc_set_dynamic_params (ceaudenc))
    goto fail_out;

  if (!gst_ce_audenc_get_buffer_info (ceaudenc))
    goto fail_out;

  /* Free previous input buffer */
  if (priv->inbuf)
    gst_buffer_unref (priv->inbuf);

  /* Allocate contiguous input buffer */
  if (!gst_ce_audenc_allocate_frame (ceaudenc, &priv->inbuf,
          priv->inbuf_desc.descs[0].bufSize))
    goto fail_out;

  GST_OBJECT_UNLOCK (ceaudenc);

  return TRUE;

fail_channels:
  {
    GST_WARNING_OBJECT (ceaudenc, "unsupported number of channels: %d\n",
        priv->channels);
    GST_OBJECT_UNLOCK (ceaudenc);
    return FALSE;
  }
fail_open_codec:
  {
    GST_ERROR_OBJECT (ceaudenc, "failed to open codec %s", klass->codec_name);
    GST_OBJECT_UNLOCK (ceaudenc);
    return FALSE;
  }
fail_out:
  {
    AUDENC1_delete (ceaudenc->codec_handle);
    ceaudenc->codec_handle = NULL;
    GST_OBJECT_UNLOCK (ceaudenc);
    return FALSE;
  }
}

static gboolean
gst_ce_audenc_set_format (GstAudioEncoder * encoder, GstAudioInfo * info)
{
  GstCaps *allowed_caps;
  GstBuffer *codec_data = NULL;

  GstCeAudEnc *ceaudenc = GST_CEAUDENC (encoder);
  GstCeAudEncClass *klass = GST_CEAUDENC_CLASS (G_OBJECT_GET_CLASS (ceaudenc));
  GstCeAudEncPrivate *priv = ceaudenc->priv;

  GST_DEBUG_OBJECT (ceaudenc, "extracting common audio information");
  priv->rate = GST_AUDIO_INFO_RATE (info);
  priv->depth = GST_AUDIO_INFO_DEPTH (info);
  priv->width = GST_AUDIO_INFO_WIDTH (info);
  priv->channels = GST_AUDIO_INFO_CHANNELS (info);
  priv->bpf = GST_AUDIO_INFO_BPF (info);

  /* some codecs support more than one format, first auto-choose one */
  GST_DEBUG_OBJECT (ceaudenc, "choosing an output format...");
  allowed_caps = gst_pad_get_allowed_caps (GST_AUDIO_ENCODER_SRC_PAD (encoder));
  if (!allowed_caps) {
    GST_DEBUG_OBJECT (ceaudenc, "... but no peer, using template caps");
    allowed_caps =
        gst_pad_get_pad_template_caps (GST_AUDIO_ENCODER_SRC_PAD (encoder));
  }

  allowed_caps = gst_caps_make_writable (allowed_caps);
  gst_caps_set_simple (allowed_caps,
      "rate", G_TYPE_INT, priv->rate,
      "channels", G_TYPE_INT, priv->channels, (char *) NULL);

  GST_DEBUG_OBJECT (ceaudenc, "chose caps %" GST_PTR_FORMAT, allowed_caps);

  if (klass->set_src_caps) {
    GST_DEBUG ("use custom set src caps");
    if (!klass->set_src_caps (ceaudenc, info, &allowed_caps, &codec_data))
      goto fail_set_caps;
  }

  /* Configure codec after subclass set caps to allow the subclass
   * to change some static properties according to the caps */
  if (!gst_ce_audenc_configure_codec (ceaudenc))
    goto fail_set_caps;

  if (codec_data) {
    GST_DEBUG_OBJECT (allowed_caps, "setting codec data");
    gst_caps_set_simple (allowed_caps,
        "codec_data", GST_TYPE_BUFFER, codec_data, (char *) NULL);
  }
  
  /* Truncate to the first structure and fixate any unfixed fields */
  allowed_caps = gst_caps_fixate (allowed_caps);

  if (!gst_audio_encoder_set_output_format (GST_AUDIO_ENCODER (ceaudenc),
          allowed_caps))
    goto fail_set_caps;

  /* Won't support NULL buffers */
  gst_audio_encoder_set_drainable (encoder, FALSE);
  return TRUE;

fail_set_caps:
  GST_ERROR_OBJECT (ceaudenc, "couldn't set audio format");
  return FALSE;
}

static gboolean
gst_ce_audenc_decide_allocation (GstAudioEncoder * encoder, GstQuery * query)
{
  GstCeAudEnc *ceaudenc = GST_CEAUDENC (encoder);
  GstCeAudEncPrivate *priv = ceaudenc->priv;
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  GstBufferPool *pool = NULL;
  GstStructure *config;

  g_return_val_if_fail (priv->outbuf_pool, FALSE);

  GST_LOG_OBJECT (ceaudenc, "decide allocation");
  if (!GST_AUDIO_ENCODER_CLASS (parent_class)->decide_allocation (encoder,
          query))
    return FALSE;
  
  /* use our own pool */
  pool = priv->outbuf_pool;

  /* we got configuration from our peer or the decide_allocation method,
   * parse them */
  if (gst_query_get_n_allocation_params (query) > 0)
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
  else
    gst_allocation_params_init (&params);

  /*
   * GstAllocationParams have an alignment that is a bitmask
   * so that align + 1 equals the amount of bytes to align to.
   */
  if (params.align < 31)
    params.align = 31;

  GST_DEBUG_OBJECT (ceaudenc, "allocation params %d, %d %d, %d", params.flags,
      params.align, params.padding, params.prefix);
  priv->alloc_params = params;

  GST_DEBUG_OBJECT (ceaudenc, "configuring output pool");
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, NULL, priv->outbuf_size, 1,
      priv->num_out_buffers);
  gst_buffer_pool_config_set_allocator (config, priv->allocator,
      &priv->alloc_params);
  gst_buffer_pool_set_config (GST_BUFFER_POOL_CAST (pool), config);
  gst_buffer_pool_set_active (GST_BUFFER_POOL_CAST (pool), TRUE);

  return TRUE;
}

/**
 * gst_ce_audenc_allocate_frame:
 * 
 * Allocates a CMEM buffer
 */
static gboolean
gst_ce_audenc_allocate_frame (GstCeAudEnc * ceaudenc, GstBuffer ** buf,
    guint size)
{
  GstCeAudEncPrivate *priv = ceaudenc->priv;

  /* Get allocator parameters */
  gst_audio_encoder_get_allocator ((GstAudioEncoder *) ceaudenc, NULL,
      &priv->alloc_params);

  *buf = gst_buffer_new_allocate (priv->allocator, size, &priv->alloc_params);

  if (!*buf) {
    GST_DEBUG_OBJECT (ceaudenc, "can't allocate buffer");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_ce_audenc_handle_frame (GstAudioEncoder * encoder, GstBuffer * buffer)
{
  GstCeAudEnc *ceaudenc = GST_CEAUDENC (encoder);
  GstCeAudEncClass *klass = GST_CEAUDENC_CLASS (G_OBJECT_GET_CLASS (ceaudenc));
  GstCeAudEncPrivate *priv = ceaudenc->priv;
  GstBuffer *outbuf = NULL;
  GstMapInfo info_in, info_out;
  AUDENC1_InArgs in_args;
  AUDENC1_OutArgs out_args;
  gint32 status;

  gst_buffer_map (buffer, &info_in, GST_MAP_READ);
  /* Copy input buffer to a contiguous buffer */
  if ((!priv->inbuf) || (info_in.size != priv->inbuf_desc.descs[0].bufSize)) {
    GST_DEBUG_OBJECT (ceaudenc, "creating new input buffer of size %d",
        info_in.size);
    if (priv->inbuf)
      gst_buffer_unref (priv->inbuf);
    if (!gst_ce_audenc_allocate_frame (ceaudenc, &priv->inbuf, info_in.size)) {
      gst_buffer_unmap (buffer, &info_in);
      goto fail_inbuf_alloc;
    }
    priv->inbuf_desc.descs[0].bufSize = info_in.size;
    priv->samples = info_in.size / ((priv->width / 8) * priv->channels);
  }
  gst_buffer_fill (priv->inbuf, 0, info_in.data, info_in.size);
  gst_buffer_unmap (buffer, &info_in);

  gst_buffer_map (priv->inbuf, &info_in, GST_MAP_READ);
  priv->inbuf_desc.descs[0].buf = (XDAS_Int8 *) info_in.data;
  GST_DEBUG_OBJECT (ceaudenc, "input buffer %p of size %li %d",
      priv->inbuf_desc.descs[0].buf, priv->inbuf_desc.descs[0].bufSize,
      priv->samples);

  /* Making sure the output buffer pool is configured */
  if (gst_pad_check_reconfigure (encoder->srcpad))
    gst_audio_encoder_negotiate (GST_AUDIO_ENCODER (encoder));
  /* Allocate an output buffer */
  if (gst_buffer_pool_acquire_buffer (GST_BUFFER_POOL_CAST (priv->outbuf_pool),
          &outbuf, NULL) != GST_FLOW_OK)
    goto fail_outbuf_alloc;

  gst_buffer_map (outbuf, &info_out, GST_MAP_WRITE);
  priv->outbuf_desc.descs[0].buf = (XDAS_Int8 *) info_out.data;
  GST_DEBUG_OBJECT (ceaudenc, "output buffer %p of size %li",
      priv->outbuf_desc.descs[0].buf, priv->outbuf_desc.descs[0].bufSize);

  /* Encode process */
  in_args.size = sizeof (AUDENC1_InArgs);
  in_args.numInSamples = priv->samples;
  /* ancDanta not used */
  in_args.ancData.bufSize = 0;
  in_args.ancData.buf = (XDAS_Int8 *) NULL;

  out_args.size = sizeof (AUDENC1_OutArgs);
  out_args.extendedError = 0;

  if (klass->pre_process) {
    GST_DEBUG_OBJECT (ceaudenc, "calling pre-processing");
    klass->pre_process (ceaudenc, priv->inbuf);
  }

  /* Encode the audio buffer */
  status =
      AUDENC1_process (ceaudenc->codec_handle, &priv->inbuf_desc,
      &priv->outbuf_desc, &in_args, &out_args);
  if (status != AUDENC1_EOK)
    goto fail_encode;

  if (klass->post_process) {
    GST_DEBUG_OBJECT (ceaudenc, "calling post-processing");
    klass->post_process (ceaudenc, outbuf);
  }

  gst_buffer_unmap (priv->inbuf, &info_in);
  gst_buffer_unmap (outbuf, &info_out);

  GST_DEBUG_OBJECT (ceaudenc,
      "audio encoder generated bytes %li, consumed %li samples",
      out_args.bytesGenerated, out_args.numInSamples);

  gst_ce_slice_buffer_resize (GST_CE_SLICE_BUFFER_POOL_CAST (priv->outbuf_pool),
      outbuf, out_args.bytesGenerated);

  return gst_audio_encoder_finish_frame (GST_AUDIO_ENCODER (ceaudenc), outbuf,
      priv->samples);

fail_inbuf_alloc:
  {
    GST_ERROR_OBJECT (ceaudenc, "failed to allocate input buffer");
    return GST_FLOW_ERROR;
  }
fail_outbuf_alloc:
  {
    gst_buffer_unmap (priv->inbuf, &info_in);
    GST_ERROR_OBJECT (ceaudenc, "failed to allocate output buffer");
    return GST_FLOW_ERROR;
  }
fail_encode:
  {
    gst_buffer_unmap (priv->inbuf, &info_in);
    gst_buffer_unmap (outbuf, &info_out);
    GST_ERROR_OBJECT (ceaudenc,
        "Failed encode process with extended error: 0x%x %d",
        (gint) out_args.extendedError, status);

    return GST_FLOW_ERROR;
  }
}

static void
gst_ce_audenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstCeAudEnc *ceaudenc;
  AUDENC1_Params *params;
  AUDENC1_DynamicParams *dyn_params;
  gboolean set_params = FALSE;

  /* Get a pointer of the right type. */
  ceaudenc = GST_CEAUDENC (object);
  
  g_return_if_fail(ceaudenc->codec_params);
  g_return_if_fail(ceaudenc->codec_dyn_params);

  params = (AUDENC1_Params *) ceaudenc->codec_params;
  dyn_params = (AUDENC1_DynamicParams *) ceaudenc->codec_dyn_params;

  GST_OBJECT_LOCK (ceaudenc);
  /* Check the argument id to see which argument we're setting. */
  switch (prop_id) {
    case PROP_BITRATE:
      params->bitRate = g_value_get_int (value);
      dyn_params->bitRate = params->bitRate;
      set_params = TRUE;
      break;
    case PROP_MAX_BITRATE:
      if (ceaudenc->codec_handle)
        goto fail_static_prop;
      params->maxBitRate = g_value_get_int (value);
      break;
    case PROP_NUM_OUT_BUFFERS:
      ceaudenc->priv->num_out_buffers = g_value_get_int (value);
      GST_LOG_OBJECT (ceaudenc,
          "setting number of output buffers to %d",
          ceaudenc->priv->num_out_buffers);
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  if (set_params && ceaudenc->codec_handle)
    gst_ce_audenc_set_dynamic_params (ceaudenc);

  GST_OBJECT_UNLOCK (ceaudenc);
  return;

fail_static_prop:
  GST_WARNING_OBJECT (ceaudenc, "can't set static property when "
      "the codec is already configured");
  GST_OBJECT_UNLOCK (ceaudenc);
  return;
}

/* The set function is simply the inverse of the get fuction. */
static void
gst_ce_audenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstCeAudEnc *ceaudenc;
  AUDENC1_Params *params;
  AUDENC1_DynamicParams *dyn_params;

  /* It's not null if we got it, but it might not be ours */
  ceaudenc = GST_CEAUDENC (object);

  if ((!ceaudenc->codec_params) || (!ceaudenc->codec_dyn_params)) {
    GST_WARNING_OBJECT (ceaudenc, "couldn't set property");
    return;
  }

  params = (AUDENC1_Params *) ceaudenc->codec_params;
  dyn_params = (AUDENC1_DynamicParams *) ceaudenc->codec_dyn_params;

  GST_OBJECT_LOCK (ceaudenc);
  switch (prop_id) {
    case PROP_BITRATE:
      g_value_set_int (value, params->bitRate);
      break;
    case PROP_MAX_BITRATE:
      g_value_set_int (value, params->maxBitRate);
      break;
    case PROP_NUM_OUT_BUFFERS:
      g_value_set_int (value, ceaudenc->priv->num_out_buffers);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (ceaudenc);
}

static gboolean
gst_ce_audenc_open (GstAudioEncoder * encoder)
{
  GstCeAudEnc *ceaudenc = GST_CEAUDENC (encoder);
  GstCeAudEncPrivate *priv = ceaudenc->priv;

  GST_DEBUG_OBJECT (ceaudenc, "opening %s Engine", CODEC_ENGINE);
  /* reset, load, and start DSP Engine */
  if ((priv->engine_handle =
          Engine_open ((Char *) CODEC_ENGINE, NULL, NULL)) == NULL)
    goto fail_engine_open;

  if (priv->allocator)
    gst_object_unref (priv->allocator);

  GST_DEBUG_OBJECT (ceaudenc, "getting CMEM allocator");
  priv->allocator = gst_allocator_find ("ContiguousMemory");
  if (!priv->allocator)
    goto fail_no_allocator;

  priv->outbuf_pool = gst_ce_slice_buffer_pool_new();
  if (!priv->outbuf_pool)
    goto fail_pool;

  GST_DEBUG_OBJECT (ceaudenc, "creating slice buffer pool");
  return TRUE;

  /* Errors */
fail_engine_open:
  {
    GST_ELEMENT_ERROR (ceaudenc, STREAM, CODEC_NOT_FOUND, (NULL),
        ("failed to open codec engine \"%s\"", CODEC_ENGINE));
    return FALSE;
  }
fail_no_allocator:
  {
    Engine_close (priv->engine_handle);
    priv->engine_handle = NULL;
    GST_WARNING_OBJECT (ceaudenc, "can't find the CMEM allocator");
    return FALSE;
  }
fail_pool:
  {
    gst_object_unref (priv->outbuf_pool);
    priv->outbuf_pool = NULL;    
    Engine_close (priv->engine_handle);
    priv->engine_handle = NULL;
    GST_WARNING_OBJECT (ceaudenc, "can't create slice buffer pool");
    return FALSE;
  }
  return TRUE;
}

static gboolean
gst_ce_audenc_close (GstAudioEncoder * encoder)
{
  GstCeAudEnc *ceaudenc = GST_CEAUDENC (encoder);
  GstCeAudEncPrivate *priv = ceaudenc->priv;

  if (priv->engine_handle) {
    GST_DEBUG_OBJECT (ceaudenc, "closing codec engine %p\n",
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
gst_ce_audenc_stop (GstAudioEncoder * encoder)
{
  GstCeAudEnc *ceaudenc = GST_CEAUDENC (encoder);
  GstCeAudEncPrivate *priv = ceaudenc->priv;

  if (priv->inbuf)
    gst_buffer_unref (priv->inbuf);

  return gst_ce_audenc_reset (encoder);
}

static gboolean
gst_ce_audenc_reset (GstAudioEncoder * encoder)
{
  GstCeAudEnc *ceaudenc = GST_CEAUDENC (encoder);
  GstCeAudEncPrivate *priv = ceaudenc->priv;
  GstCeAudEncClass *klass = GST_CEAUDENC_CLASS (G_OBJECT_GET_CLASS (ceaudenc));

  AUDENC1_Params *params = ceaudenc->codec_params;
  AUDENC1_DynamicParams *dyn_params = ceaudenc->codec_dyn_params;

  g_return_val_if_fail (params, FALSE);
  g_return_val_if_fail (dyn_params, FALSE);

  if (ceaudenc->codec_handle) {
    AUDENC1_delete (ceaudenc->codec_handle);
    ceaudenc->codec_handle = NULL;
  }

  GST_OBJECT_LOCK (ceaudenc);
  priv->num_out_buffers = PROP_NUM_OUT_BUFFERS_DEFAULT;
  /* Set default values for codec static params */
  params->sampleRate = SAMPLE_RATE_DEFAULT;
  params->bitRate = PROP_BITRATE_DEFAULT;
  params->channelMode = IAUDIO_2_0;
  params->dataEndianness = XDM_LE_16;
  params->encMode = IAUDIO_CBR;
  params->inputFormat = IAUDIO_INTERLEAVED;
  params->inputBitsPerSample = INPUT_BITS_PER_SAMPLE_DEFAULT;
  params->maxBitRate = PROP_MAX_BITRATE_DEFAULT;
  params->dualMonoMode = IAUDIO_DUALMONO_LR;
  params->crcFlag = XDAS_FALSE;
  params->ancFlag = XDAS_FALSE;
  params->lfeFlag = XDAS_FALSE;

  /* Set default values for codec dynamic params */
  dyn_params->bitRate = PROP_BITRATE_DEFAULT;
  dyn_params->sampleRate = SAMPLE_RATE_DEFAULT;
  dyn_params->channelMode = IAUDIO_2_0;
  dyn_params->lfeFlag = XDAS_FALSE;
  dyn_params->dualMonoMode = IAUDIO_DUALMONO_LR;
  dyn_params->inputBitsPerSample = INPUT_BITS_PER_SAMPLE_DEFAULT;

  GST_OBJECT_UNLOCK (ceaudenc);

  /* Configure specific codec */
  if (klass->reset) {
    GST_DEBUG_OBJECT (ceaudenc, "configuring codec");
    klass->reset (ceaudenc);
  }

  return TRUE;
}

/* Set current dynamic parameters */
static gboolean
gst_ce_audenc_set_dynamic_params (GstCeAudEnc * ceaudenc)
{
  AUDENC1_Status enc_status;
  gint ret;

  g_return_val_if_fail (ceaudenc->codec_handle, FALSE);
  g_return_val_if_fail (ceaudenc->codec_dyn_params, FALSE);

  enc_status.size = sizeof (AUDENC1_Status);
  enc_status.data.buf = NULL;

  ret = AUDENC1_control (ceaudenc->codec_handle, XDM_SETPARAMS,
      ceaudenc->codec_dyn_params, &enc_status);
  if (ret != AUDENC1_EOK) {
    GST_WARNING_OBJECT (ceaudenc, "Failed to set dynamic parameters, "
        "status error %x, %d", (unsigned int) enc_status.extendedError, ret);
    return FALSE;
  }

  return TRUE;
}

/* Get buffer information from audio codec */
static gboolean
gst_ce_audenc_get_buffer_info (GstCeAudEnc * ceaudenc)
{
  GstCeAudEncPrivate *priv = ceaudenc->priv;
  AUDENC1_Status enc_status;
  gint i, ret;

  g_return_val_if_fail (ceaudenc->codec_handle, FALSE);
  g_return_val_if_fail (ceaudenc->codec_dyn_params, FALSE);

  enc_status.size = sizeof (AUDENC1_Status);
  enc_status.data.buf = NULL;

  ret = AUDENC1_control (ceaudenc->codec_handle, XDM_GETBUFINFO,
      ceaudenc->codec_dyn_params, &enc_status);
  if (ret != AUDENC1_EOK) {
    GST_ERROR_OBJECT (ceaudenc, "failed to get buffer information, "
        "status error %x, %d", (guint) enc_status.extendedError, ret);
    return FALSE;
  }

  priv->inbuf_desc.numBufs = enc_status.bufInfo.minNumInBufs;
  for (i = 0; i < enc_status.bufInfo.minNumInBufs; i++) {
    priv->inbuf_desc.descs[i].bufSize = enc_status.bufInfo.minInBufSize[i];
    GST_DEBUG_OBJECT (ceaudenc, "size of input buffer [%d] = %li", i,
        priv->inbuf_desc.descs[i].bufSize);
  }

  priv->outbuf_size = enc_status.bufInfo.minOutBufSize[0];
  priv->outbuf_desc.numBufs = 1;
  priv->outbuf_desc.descs[0].bufSize = enc_status.bufInfo.minOutBufSize[0];

  GST_DEBUG_OBJECT (ceaudenc, "output buffer size = %d", priv->outbuf_size);

  return TRUE;
}

/**
 * gst_ce_audenc_set_frame_samples:
 * @cevidenc: a #GstCeAudEnc
 * @min_samples: the #GstBuffer containing the 
 *        encoding header.
 * @max_samples: maximum number of samples per frame that can be handed by the codec.
 * @min_samples: minimum number of samples per frame that can be handed by the codec.
 * 
 * Lets #GstCeAudEnc sub-classes to set the audio codec samples per buffer 
 * capabilities. If @max_samples is equal to @min_samples, means the codec
 * cannot handle less samples, the leftover samples will simply be discarded.
 *
 */
void
gst_ce_audenc_set_frame_samples (GstCeAudEnc * ceaudenc, gint min_samples,
    gint max_samples)
{
  GstAudioEncoder *encoder = GST_AUDIO_ENCODER (ceaudenc);
  GstCeAudEncPrivate *priv = ceaudenc->priv;
  priv->min_samples = min_samples;
  priv->max_samples = max_samples;
  priv->samples = max_samples;
  /* report needs to base class */
  gst_audio_encoder_set_frame_samples_min (encoder, priv->min_samples);
  gst_audio_encoder_set_frame_samples_max (encoder, priv->max_samples);
  gst_audio_encoder_set_frame_max (encoder, 1);
  if (min_samples == max_samples)
    gst_audio_encoder_set_hard_min (encoder, TRUE);
}
