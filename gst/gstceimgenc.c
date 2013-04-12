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

#include <ti/sdo/ce/osal/Memory.h>

enum
{
  PROP_0,
  PROP_MAX_SCANS,
  PROP_FORCE_CHROMA_FORMAT,
  PROP_DATA_ENDIANNESS,
  PROP_QUALITY_VALUE,
  PROP_CODEC_BASE
};

#define  PROP_MAX_SCANS_DEFAULT 15
#define  PROP_FORCE_CHROMA_FORMAT_DEFAULT XDM_YUV_420P
#define  PROP_DATA_ENDIANNESS_DEFAULT XDM_BYTE
#define  PROP_QUALITY_VALUE_DEFAULT 50

#define GST_CE_IMGENC_CHROMA_FORMAT_TYPE (gst_ceimgenc_chroma_format_get_type())
static GType
gst_ceimgenc_chroma_format_get_type (void)
{
  static GType chroma_format_type = 0;

  static const GEnumValue chroma_format_types[] = {
    {XDM_CHROMA_NA, "Chroma format not applicable", "NA"},
    {XDM_YUV_420P, "YUV 4:2:0 planer", "YUV420P"},
    {XDM_YUV_422P, "YUV 4:2:2 planer", "YUV422P"},
    {XDM_YUV_422IBE, "YUV 4:2:2 interleaved big endian", "YUV422IBE"},
    {XDM_YUV_422ILE, "YUV 4:2:2 interleaved little endian", "YUV422ILE"},
    {XDM_YUV_444P, "YUV 4:4:4 planer", "YUV444P"},
    {XDM_YUV_411P, "YUV 4:1:1 planer", "YUV411P"},
    {XDM_YUV_420SP, "YUV 4:2:0 semi-planer", "YUV420SP"},
    {XDM_YUV_444ILE, "YUV 4:4:4 interleaved little endian", "YUV444ILE"},
    {XDM_GRAY, "Gray format", "Gray"},
    {XDM_RGB, "RGB color format", "RGB"},
    {XDM_RGB555, "RGB 555 color format", "RGB555"},
    {XDM_RGB565, "RGB 565 color format", "RGB565"},
    {XDM_ARGB8888, "Alpha plane", "ARGB8888"},
    {XDM_CHROMAFORMAT_DEFAULT, "Default setting", "Default"},
    {0, NULL, NULL}
  };

  if (!chroma_format_type) {
    chroma_format_type =
        g_enum_register_static ("GstCEImgEncChromaFormat", chroma_format_types);
  }
  return chroma_format_type;
}

#define GST_CE_IMGENC_DATA_FORMAT_TYPE (gst_ceimgenc_data_format_get_type())
static GType
gst_ceimgenc_data_format_get_type (void)
{
  static GType data_format_type = 0;

  static const GEnumValue data_format_types[] = {
    {XDM_BYTE, "Big endian stream", "BYTE"},
    {XDM_LE_16, "16 bit little endian stream", "LE16"},
    {XDM_LE_32, "32 bit little endian stream", "LE32"},
    {XDM_LE_64, "64 bit little endian stream", "LE64"},
    {XDM_BE_16, "16 bit big endian stream", "BE16"},
    {XDM_BE_32, "32 bit big endian stream", "BE32"},
    {XDM_BE_64, "64 bit big endian stream", "BE64"},
    {0, NULL, NULL}
  };

  if (!data_format_type) {
    data_format_type =
        g_enum_register_static ("GstCEImgEncDataFormat", data_format_types);
  }
  return data_format_type;
}

#define GST_CEIMGENC_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_CEIMGENC, GstCEImgEncPrivate))

struct _GstCEImgEncPrivate
{
  gboolean first_buffer;

  XDAS_Int32 frameWidth;
  XDAS_Int32 frameHeight;
  XDAS_Int32 framePitch;

  gint32 outbuf_size;
  GstVideoFormat video_format;
  GstVideoCodecState *input_state;

  /* Handle to the CMEM allocator */
  GstAllocator *allocator;
  GstAllocationParams alloc_params;

  /* Codec Data */
  Engine_Handle engine_handle;
  XDM1_BufDesc inbuf_desc;
  XDM1_BufDesc outbuf_desc;
};

/* A number of function prototypes are given so we can refer to them later. */
static void gst_ceimgenc_finalize (GObject * object);

static gboolean gst_ceimgenc_open (GstVideoEncoder * encoder);
static gboolean gst_ceimgenc_close (GstVideoEncoder * encoder);
static gboolean gst_ceimgenc_stop (GstVideoEncoder * encoder);
static gboolean gst_ceimgenc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static gboolean gst_ceimgenc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);
static GstFlowReturn gst_ceimgenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static gboolean gst_ceimgenc_reset (GstVideoEncoder * encoder);
static void gst_ceimgenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ceimgenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define gst_ceimgenc_parent_class parent_class
G_DEFINE_TYPE (GstCEImgEnc, gst_ceimgenc, GST_TYPE_VIDEO_ENCODER);

static void
gst_ceimgenc_class_init (GstCEImgEncClass * klass)
{
  GObjectClass *gobject_class;
  GstVideoEncoderClass *venc_class;
  g_print ("Class init");
  gobject_class = (GObjectClass *) klass;
  venc_class = (GstVideoEncoderClass *) klass;

  g_type_class_add_private (klass, sizeof (GstCEImgEncPrivate));

  gobject_class->set_property = gst_ceimgenc_set_property;
  gobject_class->get_property = gst_ceimgenc_get_property;

  g_object_class_install_property (gobject_class, PROP_MAX_SCANS,
      g_param_spec_int ("maxScans", "Maximum number of scans",
          "Maximum number of scans", 1, 100,
          PROP_MAX_SCANS_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_FORCE_CHROMA_FORMAT,
      g_param_spec_enum ("forceChromaFormat", "Force chroma format",
          "Force encoding in given Chroma format",
          GST_CE_IMGENC_CHROMA_FORMAT_TYPE, PROP_FORCE_CHROMA_FORMAT_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DATA_ENDIANNESS,
      g_param_spec_enum ("dataEndianness", "Data endianness",
          "Endianness of output data", GST_CE_IMGENC_DATA_FORMAT_TYPE,
          PROP_DATA_ENDIANNESS_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_QUALITY_VALUE,
      g_param_spec_int ("qValue", "Quality value",
          "Quality factor for encoder", 0, 100,
          PROP_QUALITY_VALUE_DEFAULT, G_PARAM_READWRITE));

  venc_class->open = gst_ceimgenc_open;
  venc_class->close = gst_ceimgenc_close;
  venc_class->stop = gst_ceimgenc_stop;
  venc_class->handle_frame = gst_ceimgenc_handle_frame;
  venc_class->set_format = gst_ceimgenc_set_format;
  venc_class->propose_allocation = gst_ceimgenc_propose_allocation;

  gobject_class->finalize = gst_ceimgenc_finalize;
}

static void
gst_ceimgenc_init (GstCEImgEnc * ceimgenc)
{
  GstCEImgEncPrivate *priv;
  g_print ("init");
  GST_DEBUG_OBJECT (ceimgenc, "initialize encoder");

  priv = ceimgenc->priv = GST_CEIMGENC_GET_PRIVATE (ceimgenc);

  /* Allocate the codec params */
  if (!ceimgenc->codec_params) {
    GST_DEBUG_OBJECT (ceimgenc, "allocating codec params");
    ceimgenc->codec_params = g_malloc0 (sizeof (IMGENC1_Params));
    if (ceimgenc->codec_params == NULL) {
      GST_WARNING_OBJECT (ceimgenc, "failed to allocate IMGENC1_Params");
      return;
    }
    ceimgenc->codec_params->size = sizeof (IMGENC1_Params);
  }

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

static void
gst_ceimgenc_finalize (GObject * object)
{
  GstCEImgEnc *ceimgenc = (GstCEImgEnc *) (object);
  g_print ("Finalize");
  /* Allocate the codec params */
  if (ceimgenc->codec_params) {
    g_free (ceimgenc->codec_params);
    ceimgenc->codec_params = NULL;
  }

  if (ceimgenc->codec_dyn_params) {
    g_free (ceimgenc->codec_dyn_params);
    ceimgenc->codec_dyn_params = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_ceimgenc_configure_codec:
 * Based on the negotiated format, create and initialize the 
 * codec instance*/
static gboolean
gst_ceimgenc_configure_codec (GstCEImgEnc * ceimgenc)
{

  GstCEImgEncClass *klass;
  GstCEImgEncPrivate *priv;
  IMGENC1_Status enc_status;
  IMGENC1_Params *params;
  IMGENC1_DynamicParams *dyn_params;
  gint ret, i;
  g_print ("configure codec");
  klass = (GstCEImgEncClass *) G_OBJECT_GET_CLASS (ceimgenc);
  priv = ceimgenc->priv;
  params = ceimgenc->codec_params;
  dyn_params = ceimgenc->codec_dyn_params;

  g_return_val_if_fail (params, FALSE);
  g_return_val_if_fail (dyn_params, FALSE);
  g_return_val_if_fail (klass->codec_name, FALSE);

/* Set the caps on the parameters of the encoder */
  switch (priv->video_format) {
    case GST_VIDEO_FORMAT_UYVY:
      dyn_params->inputChromaFormat = XDM_YUV_422ILE;
      dyn_params->captureWidth = priv->framePitch / 2;
      break;
    case GST_VIDEO_FORMAT_NV12:
      dyn_params->inputChromaFormat = XDM_YUV_420SP;
      dyn_params->captureWidth = priv->framePitch;
      break;
    default:
      GST_ELEMENT_ERROR (ceimgenc, STREAM, NOT_IMPLEMENTED,
          ("unsupported format in video stream: %d\n",
              priv->video_format), (NULL));
      return FALSE;
  }

  GST_OBJECT_LOCK (ceimgenc);

  params->maxWidth = priv->frameWidth;
  params->maxHeight = priv->frameHeight;

  dyn_params->inputWidth = priv->frameWidth;
  dyn_params->inputHeight = priv->frameHeight;

  if (ceimgenc->codec_handle) {
    GST_DEBUG_OBJECT (ceimgenc, "Closing old codec session");
    IMGENC1_delete (ceimgenc->codec_handle);
  }

  GST_DEBUG_OBJECT (ceimgenc, "Create the codec handle");
  ceimgenc->codec_handle = IMGENC1_create (priv->engine_handle,
      (Char *) klass->codec_name, params);
  if (!ceimgenc->codec_handle)
    goto fail_open_codec;

  enc_status.size = sizeof (IMGENC1_Status);
  enc_status.data.buf = NULL;

  GST_DEBUG_OBJECT (ceimgenc, "Set codec dynamic parameters");
  ret = IMGENC1_control (ceimgenc->codec_handle, XDM_SETPARAMS,
      dyn_params, &enc_status);
  if (ret != IMGENC1_EOK)
    goto fail_control_params;

  GST_OBJECT_UNLOCK (ceimgenc);

  /* Get buffer information from image encoder */
  ret = IMGENC1_control (ceimgenc->codec_handle, XDM_GETBUFINFO,
      dyn_params, &enc_status);
  if (ret != IMGENC1_EOK)
    goto fail_control_getinfo;

  for (i = 0; i < enc_status.bufInfo.minNumInBufs; i++) {
    priv->inbuf_desc.descs[i].bufSize = enc_status.bufInfo.minInBufSize[i];
    GST_DEBUG_OBJECT (ceimgenc, "size of input buffer [%d] = %li", i,
        priv->inbuf_desc.descs[i].bufSize);
  }

  priv->outbuf_size = enc_status.bufInfo.minOutBufSize[0];
  priv->outbuf_desc.numBufs = 1;
  priv->outbuf_desc.descs->bufSize = (XDAS_Int32) priv->outbuf_size;
  priv->outbuf_desc.descs->accessMask = XDM_ACCESSMODE_WRITE;
  GST_DEBUG_OBJECT (ceimgenc, "output buffer size = %d", priv->outbuf_size);

  return TRUE;

fail_open_codec:
  {
    GST_ERROR_OBJECT (ceimgenc, "Failed to open codec %s", klass->codec_name);
    GST_OBJECT_UNLOCK (ceimgenc);
    return FALSE;
  }
fail_control_params:
  {
    GST_ERROR_OBJECT (ceimgenc, "Failed to set dynamic parameters, "
        "status error %x, %d", (guint) enc_status.extendedError, ret);
    GST_OBJECT_UNLOCK (ceimgenc);
    IMGENC1_delete (ceimgenc->codec_handle);
    ceimgenc->codec_handle = NULL;
    return FALSE;
  }
fail_control_getinfo:
  {
    GST_ERROR_OBJECT (ceimgenc, "Failed to get buffer information, "
        "status error %x, %d", (guint) enc_status.extendedError, ret);
    IMGENC1_delete (ceimgenc->codec_handle);
    ceimgenc->codec_handle = NULL;
    return FALSE;
  }
}

static gboolean
gst_ceimgenc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  GstCaps *allowed_caps;
  GstVideoCodecState *output_format;

  GstCEImgEnc *ceimgenc = (GstCEImgEnc *) encoder;
  GstCEImgEncClass *klass = (GstCEImgEncClass *) G_OBJECT_GET_CLASS (ceimgenc);
  GstCEImgEncPrivate *priv = ceimgenc->priv;
  g_print ("Set format");
  GST_DEBUG_OBJECT (ceimgenc, "Extracting common image information");

  /* Prepare the input buffer descriptor */
  priv->frameWidth = GST_VIDEO_INFO_WIDTH (&state->info);
  priv->frameHeight = GST_VIDEO_INFO_HEIGHT (&state->info);
  priv->framePitch = GST_VIDEO_INFO_PLANE_STRIDE (&state->info, 0);
  priv->inbuf_desc.numBufs = GST_VIDEO_INFO_N_PLANES (&state->info);

  priv->video_format = GST_VIDEO_INFO_FORMAT (&state->info);

  GST_DEBUG_OBJECT (ceimgenc, "input buffer format: width=%li, height=%li,"
      " pitch=%li", priv->frameWidth, priv->frameHeight, priv->framePitch);

  if (!gst_ceimgenc_configure_codec (ceimgenc))
    goto fail_set_caps;

  /* some codecs support more than one format, first auto-choose one */
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

  if (klass->set_src_caps) {
    GST_DEBUG ("Use custom set src caps");
    if (!klass->set_src_caps (ceimgenc, &allowed_caps))
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

  gst_video_codec_state_unref (output_format);

  return TRUE;

fail_set_caps:
  GST_ERROR_OBJECT (ceimgenc, "couldn't set image format");
  return FALSE;
}

static gboolean
gst_ceimgenc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  GstCEImgEnc *ceimgenc = (GstCEImgEnc *) encoder;
  GstCEImgEncPrivate *priv = ceimgenc->priv;
  GstAllocationParams params;
  g_print ("propose allocation");
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
 * gst_ceimgenc_allocate_output_frame
 * 
 * Allocates a CMEM output buffer 
 */
static gboolean
gst_ceimgenc_allocate_output_frame (GstCEImgEnc * ceimgenc, GstBuffer ** buf)
{
  GstCEImgEncPrivate *priv = ceimgenc->priv;
  g_print ("allocate outpu frame");
  /*Get allocator parameters */
  gst_video_encoder_get_allocator ((GstVideoEncoder *) ceimgenc,
      &priv->allocator, &priv->alloc_params);

  *buf = gst_buffer_new_allocate (priv->allocator, priv->outbuf_size,
      &priv->alloc_params);

  if (!*buf) {
    GST_DEBUG_OBJECT (ceimgenc, "Can't alloc output buffer");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_ceimgenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstCEImgEnc *ceimgenc = (GstCEImgEnc *) encoder;
  GstCEImgEncPrivate *priv = ceimgenc->priv;
  GstCEImgEncClass *klass = (GstCEImgEncClass *) G_OBJECT_GET_CLASS (ceimgenc);
  GstVideoInfo *info = &priv->input_state->info;
  //GstVideoInfo info_data = *info;
  GstVideoFrame vframe;
  GstMapInfo info_out;
  GstBuffer *outbuf = NULL;
  gboolean is_contiguous = FALSE;
  gint ret = 0;
  gint i;
  gint32 phys;

  IMGENC1_InArgs in_args;
  IMGENC1_OutArgs out_args;
  g_print ("Handle frame");
  /* Fill planes pointer */
  if (!gst_video_frame_map (&vframe, info, frame->input_buffer, GST_MAP_READ))
    goto fail_map;

  for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (&vframe); i++) {
    g_print ("%d", i);
    priv->inbuf_desc.descs[i].buf = GST_VIDEO_FRAME_PLANE_DATA (&vframe, i);
  }
  /* $
   * TODO
   * No CMEM contiguous buffers should be registered to Codec Engine
   * How should we manage the buffer registration?
   */
  phys =
      Memory_getBufferPhysicalAddress (GST_VIDEO_FRAME_COMP_DATA (&vframe, 0),
      GST_VIDEO_FRAME_SIZE (&vframe), (Bool *) & is_contiguous);

  gst_video_frame_unmap (&vframe);
  /* $
   * TODO
   * Failing if input buffer is not contiguous. Should copy the buffer
   * instead of fail?
   */
  if (!is_contiguous)
    goto fail_no_contiguous_buffer;

  /* Allocate output buffer */
  if (!gst_ceimgenc_allocate_output_frame (ceimgenc, &outbuf))
    goto fail_alloc;

  if (!gst_buffer_map (outbuf, &info_out, GST_MAP_WRITE)) {
    goto fail_alloc;
  }

  priv->outbuf_desc.descs->buf = (XDAS_Int8 *) & (info_out.data);

  /* Set output and input arguments for the encode process */
  in_args.size = sizeof (IIMGENC1_InArgs);
  out_args.size = sizeof (IMGENC1_OutArgs);

  /* Pre-encode process */
  if (klass->pre_process && !klass->pre_process (ceimgenc, frame->input_buffer))
    goto fail_pre_encode;

  /* Encode process */
  ret =
      IMGENC1_process (ceimgenc->codec_handle, &priv->inbuf_desc,
      &priv->outbuf_desc, &in_args, &out_args);
  if (ret != IMGENC1_EOK)
    goto fail_encode;
  g_print ("Processed");
  GST_DEBUG_OBJECT (ceimgenc, "encoded an output buffer of size %li at addr %p",
      out_args.bytesGenerated, priv->outbuf_desc.descs->buf);

  gst_buffer_unmap (outbuf, &info_out);

  gst_buffer_set_size (outbuf, out_args.bytesGenerated);

  /* Post-encode process */
  if (klass->post_process && !klass->post_process (ceimgenc, outbuf))
    goto fail_post_encode;

  /*Mark when finish to process the first buffer */
  //if (priv->first_buffer)
  //  priv->first_buffer = FALSE;

  GST_DEBUG_OBJECT (ceimgenc, "frame encoded succesfully");

  /* Get oldest frame */
  gst_video_codec_frame_unref (frame);
  frame = gst_video_encoder_get_oldest_frame (encoder);
  frame->output_buffer = outbuf;

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
    GST_ERROR_OBJECT (ceimgenc, "Failed to get output buffer");
    return GST_FLOW_ERROR;
  }
fail_pre_encode:
  {
    gst_buffer_unmap (outbuf, &info_out);
    GST_ERROR_OBJECT (ceimgenc, "Failed pre-encode process");
    return GST_FLOW_ERROR;
  }
fail_encode:
  {
    gst_buffer_unmap (outbuf, &info_out);
    GST_ERROR_OBJECT (ceimgenc,
        "Failed encode process with extended error: 0x%x",
        (unsigned int) out_args.extendedError);
    return GST_FLOW_ERROR;
  }
fail_post_encode:
  {
    GST_ERROR_OBJECT (ceimgenc, "Failed post-encode process");
    return GST_FLOW_ERROR;
  }
}

static void
gst_ceimgenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstCEImgEnc *ceimgenc;
  GstCEImgEncClass *klass;
  IMGENC1_Params *params;
  IMGENC1_DynamicParams *dyn_params;
  IMGENC1_Status enc_status;
  gboolean set_params = FALSE;

  gint ret;
  g_print ("Set property");
  /* Get a pointer of the right type. */
  ceimgenc = (GstCEImgEnc *) (object);
  klass = (GstCEImgEncClass *) G_OBJECT_GET_CLASS (ceimgenc);

  if ((!ceimgenc->codec_params) || (!ceimgenc->codec_dyn_params)) {
    GST_WARNING_OBJECT (ceimgenc, "couldn't set property");
    return;
  }

  params = (IMGENC1_Params *) ceimgenc->codec_params;
  dyn_params = (IMGENC1_DynamicParams *) ceimgenc->codec_dyn_params;

  GST_OBJECT_LOCK (ceimgenc);
  /* Check the argument id to see which argument we're setting. */
  switch (prop_id) {
    case PROP_MAX_SCANS:
      if (!ceimgenc->codec_handle) {
        params->maxScans = g_value_get_enum (value);
        GST_LOG_OBJECT (ceimgenc,
            "setting maximum number of scans to %li", params->maxScans);
      } else {
        goto fail_static_prop;
      }
      break;
    case PROP_FORCE_CHROMA_FORMAT:
      if (!ceimgenc->codec_handle) {
        params->forceChromaFormat = g_value_get_enum (value);
        GST_LOG_OBJECT (ceimgenc,
            "setting force chroma format to %li", params->forceChromaFormat);
      } else {
        goto fail_static_prop;
      }
      break;
    case PROP_DATA_ENDIANNESS:
      if (!ceimgenc->codec_handle) {
        params->dataEndianness = g_value_get_enum (value);
        GST_LOG_OBJECT (ceimgenc,
            "setting data endianness to %li", params->dataEndianness);
      } else {
        goto fail_static_prop;
      }
      break;
    case PROP_QUALITY_VALUE:
      dyn_params->qValue = g_value_get_int (value);
      GST_LOG_OBJECT (ceimgenc,
          "setting quality value to %li", dyn_params->qValue);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  if (set_params && ceimgenc->codec_handle) {
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

fail_static_prop:
  GST_WARNING_OBJECT (ceimgenc, "can't set static property when "
      "the codec is already configured");
  GST_OBJECT_UNLOCK (ceimgenc);
}

/* The set function is simply the inverse of the get fuction. */
static void
gst_ceimgenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstCEImgEnc *ceimgenc;
  GstCEImgEncClass *klass;
  IMGENC1_Params *params;
  IMGENC1_DynamicParams *dyn_params;
  g_print ("Get property");
  /* It's not null if we got it, but it might not be ours */
  ceimgenc = (GstCEImgEnc *) (object);
  klass = (GstCEImgEncClass *) G_OBJECT_GET_CLASS (ceimgenc);

  if ((!ceimgenc->codec_params) || (!ceimgenc->codec_dyn_params)) {
    GST_WARNING_OBJECT (ceimgenc, "couldn't get property");
    return;
  }

  params = (IMGENC1_Params *) ceimgenc->codec_params;
  dyn_params = (IMGENC1_DynamicParams *) ceimgenc->codec_dyn_params;

  GST_OBJECT_LOCK (ceimgenc);
  switch (prop_id) {
    case PROP_MAX_SCANS:
      g_value_set_enum (value, params->maxScans);
      break;
    case PROP_FORCE_CHROMA_FORMAT:
      g_value_set_enum (value, params->forceChromaFormat);
      break;
    case PROP_DATA_ENDIANNESS:
      g_value_set_enum (value, params->dataEndianness);
      break;
    case PROP_QUALITY_VALUE:
      g_value_set_int (value, dyn_params->qValue);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (ceimgenc);
}

static gboolean
gst_ceimgenc_open (GstVideoEncoder * encoder)
{
  GstCEImgEnc *ceimgenc = (GstCEImgEnc *) encoder;
  GstCEImgEncPrivate *priv = ceimgenc->priv;
  g_print ("open");
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

  return TRUE;
}

static gboolean
gst_ceimgenc_close (GstVideoEncoder * encoder)
{
  GstCEImgEnc *ceimgenc = (GstCEImgEnc *) encoder;
  GstCEImgEncPrivate *priv = ceimgenc->priv;
  g_print ("close");
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

  return TRUE;
}

static gboolean
gst_ceimgenc_stop (GstVideoEncoder * encoder)
{
  g_print ("stop");
  return gst_ceimgenc_reset (encoder);
}

static gboolean
gst_ceimgenc_reset (GstVideoEncoder * encoder)
{
  GstCEImgEnc *ceimgenc = (GstCEImgEnc *) encoder;
  GstCEImgEncPrivate *priv = ceimgenc->priv;
  GstCEImgEncClass *klass = (GstCEImgEncClass *) G_OBJECT_GET_CLASS (ceimgenc);

  IMGENC1_Params *params = ceimgenc->codec_params;
  IMGENC1_DynamicParams *dyn_params = ceimgenc->codec_dyn_params;
  g_print ("reset");
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

  /* Set default values for codec static params */
  params->forceChromaFormat = PROP_FORCE_CHROMA_FORMAT_DEFAULT;
  params->dataEndianness = PROP_DATA_ENDIANNESS_DEFAULT;
  params->maxScans = PROP_MAX_SCANS_DEFAULT;;

  /* Set default values for codec dynamic params */
  dyn_params->qValue = PROP_QUALITY_VALUE_DEFAULT;
  dyn_params->numAU = XDM_DEFAULT;
  dyn_params->generateHeader = XDM_ENCODE_AU;

  GST_OBJECT_UNLOCK (ceimgenc);

  /* Configure specific codec */
  if (klass->reset) {
    GST_DEBUG_OBJECT (ceimgenc, "configuring codec");
    klass->reset (ceimgenc);
  }

  return TRUE;
}
