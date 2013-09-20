/*
 * gstcejpegenc.c
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
 * SECTION:element-ce_jpegenc
 *
 * The ce_jpegenc encodes raw video/images into JPEG image
 * compressed data.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 videotestsrc num-buffers=50 ! video/x-raw, framerate='(fraction)'5/1 ! ce_jpegenc disable-eoi=true ! avimux ! filesink location=mjpeg.avi
 * ]| a pipeline to mux 5 JPEG frames per second into a 10 sec. long motion jpeg
 * avi.
 * </refsect2>
 */
#include <xdc/std.h>
#include <string.h>
#include <ti/sdo/codecs/jpegenc/ijpegenc.h>

#include "gstcejpegenc.h"

GST_DEBUG_CATEGORY_STATIC (gst_ce_jpegenc_debug);
#define GST_CAT_DEFAULT gst_ce_jpegenc_debug

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_ce_jpegenc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "   format = (string) {NV12,UYVY},"
        "   framerate=(fraction)[ 0, 120], "
        "   width=(int)[ 97, 4080 ], " "   height=(int)[ 16, 4096 ]")
    );
/* *INDENT-ON* */

static GstStaticPadTemplate gst_ce_jpegenc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
        "   framerate=(fraction)[ 0, 120], "
        "   width=(int)[ 97, 4080 ], " "   height=(int)[ 16, 4096 ]")
    );

enum
{
  PROP_BASE = 0,
  PROP_ROTATION,
  PROP_DISABLE_EOI,
};

#define JPEG_DEFAULT_ROTATION 0
#define JPEG_DEFAULT_DISABLE_EOI 0
#define JPEG_DEFAULT_RST_INTERVAL 84

enum
{
  GST_CE_JPEGENC_ROTATE_0 = 0,
  GST_CE_JPEGENC_ROTATE_90 = 90,
  GST_CE_JPEGENC_ROTATE_180 = 180,
  GST_CE_JPEGENC_ROTATE_270 = 270,
};

#define GST_CE_JPEGENC_ROTATION_TYPE (gst_ce_jpegenc_rotation_get_type())
static GType
gst_ce_jpegenc_rotation_get_type (void)
{
  static GType rotation_type = 0;

  static const GEnumValue rotation_types[] = {
    {GST_CE_JPEGENC_ROTATE_0, "Rotation 0 degrees", "rotate-0"},
    {GST_CE_JPEGENC_ROTATE_90, "Rotation 90 degrees", "rotate-90"},
    {GST_CE_JPEGENC_ROTATE_180, "Rotation 180 degrees", "rotate-180"},
    {GST_CE_JPEGENC_ROTATE_270, "Rotation 270 degrees", "rotate-270"},
    {0, NULL, NULL}
  };

  if (!rotation_type) {
    rotation_type =
        g_enum_register_static ("GstCeJpegEncRotation", rotation_types);
  }
  return rotation_type;
}

static void gst_ce_jpegenc_reset (GstCeImgEnc * ce_imgenc);

static void gst_ce_jpegenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_ce_jpegenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

#define gst_ce_jpegenc_parent_class parent_class
G_DEFINE_TYPE (GstCeJpegEnc, gst_ce_jpegenc, GST_TYPE_CE_IMGENC);

/**
 * JPEG encoder class initialization function
 */
static void
gst_ce_jpegenc_class_init (GstCeJpegEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstCeImgEncClass *ce_imgenc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  ce_imgenc_class = GST_CE_IMGENC_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_ce_jpegenc_debug, "ce_jpegenc", 0,
      "CE JPEG encoding element");

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_ce_jpegenc_set_property;
  gobject_class->get_property = gst_ce_jpegenc_get_property;

  /* Initialization of the JPEG encoder properties */
  g_object_class_install_property (gobject_class, PROP_ROTATION,
      g_param_spec_enum ("rotation", "Rotation",
          "Set the rotation angle", GST_CE_JPEGENC_ROTATION_TYPE,
          JPEG_DEFAULT_ROTATION, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DISABLE_EOI,
      g_param_spec_boolean ("disable-eoi", "Disable End of Image",
          "Disable End of Image, for some video pipelines is recommended set this to true",
          FALSE, G_PARAM_READWRITE));

  /* pad templates */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ce_jpegenc_sink_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ce_jpegenc_src_pad_template));

  gst_element_class_set_static_metadata (element_class,
      "CE JPEG image/video encoder", "Codec/Encoder/Image",
      "Encode image in JPEG format",
      "Carlos Gomez <carlos.gomez@ridgerun.com>");

  ce_imgenc_class->codec_name = "jpegenc";
  ce_imgenc_class->reset = gst_ce_jpegenc_reset;
}

/**
 * JPEG encoder initialization function
 */
static void
gst_ce_jpegenc_init (GstCeJpegEnc * jpegenc)
{
  GstCeImgEnc *ce_imgenc = GST_CE_IMGENC (jpegenc);
  IJPEGENC_Params *jpeg_params = NULL;
  IJPEGENC_DynamicParams *jpeg_dyn_params = NULL;

  /* Allocate the JPEG encoder parameters */
  jpeg_params = g_malloc0 (sizeof (IJPEGENC_Params));
  if (!jpeg_params)
    goto fail_alloc;

  /* Allocate the JPEG encoder dynamic parameters */
  jpeg_dyn_params = g_malloc0 (sizeof (IJPEGENC_DynamicParams));
  if (!jpeg_dyn_params)
    goto fail_alloc;

  /* Extend the codec parameters with the JPEG parameters */
  if (ce_imgenc->codec_params) {
    GST_DEBUG_OBJECT (jpegenc, "codec params not NULL, copy and free them");
    jpeg_params->imgencParams = *ce_imgenc->codec_params;
    g_free (ce_imgenc->codec_params);
  }
  ce_imgenc->codec_params = (IMGENC1_Params *) jpeg_params;

  /* Extend the codec dynamic parameters with the JPEG dynamic parameters */
  if (ce_imgenc->codec_dyn_params) {
    GST_DEBUG_OBJECT (jpegenc,
        "codec dynamic params not NULL, copy and free them");
    jpeg_dyn_params->imgencDynamicParams = *ce_imgenc->codec_dyn_params;
    g_free (ce_imgenc->codec_dyn_params);
  }
  ce_imgenc->codec_dyn_params = (IMGENC1_DynamicParams *) jpeg_dyn_params;

  /* Resize the original parameters to include the extended parameters */
  ce_imgenc->codec_params->size = sizeof (IJPEGENC_Params);
  ce_imgenc->codec_dyn_params->size = sizeof (IJPEGENC_DynamicParams);

  gst_ce_jpegenc_reset (ce_imgenc);

  return;

fail_alloc:
  {
    GST_WARNING_OBJECT (jpegenc, "failed to allocate JPEG params");
    if (jpeg_params)
      g_free (jpeg_params);
    if (jpeg_dyn_params)
      g_free (jpeg_dyn_params);
    return;
  }
}

/**
 * Reset JPEG encoder
 */
static void
gst_ce_jpegenc_reset (GstCeImgEnc * ce_imgenc)
{
  IJPEGENC_Params *jpeg_params;
  IJPEGENC_DynamicParams *jpeg_dyn_params;

  if ((sizeof (IJPEGENC_Params) != ce_imgenc->codec_params->size) ||
      (sizeof (IJPEGENC_DynamicParams) != ce_imgenc->codec_dyn_params->size))
    GST_WARNING_OBJECT (ce_imgenc, "there isn't JPEG extended parameters");
  return;

  jpeg_params = (IJPEGENC_Params *) ce_imgenc->codec_params;
  jpeg_dyn_params = (IJPEGENC_DynamicParams *) ce_imgenc->codec_dyn_params;

  GST_DEBUG_OBJECT (ce_imgenc, "setup JPEG defaults parameters");

  /* In order to understand the default values 
     please read the JPEG Sequential Encoder on DM365 User’s Guide */

  jpeg_params->halfBufCB = NULL;
  jpeg_params->halfBufCBarg = NULL;

  jpeg_dyn_params->disableEOI = JPEG_DEFAULT_DISABLE_EOI;
  jpeg_dyn_params->rotation = JPEG_DEFAULT_ROTATION;
  jpeg_dyn_params->rstInterval = JPEG_DEFAULT_RST_INTERVAL;
  jpeg_dyn_params->customQ = NULL;

  return;
}

/**
 * Sets custom properties to JPEG encoder
 */
static void
gst_ce_jpegenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCeImgEnc *ce_imgenc = GST_CE_IMGENC (object);
  IJPEGENC_DynamicParams *dyn_params;
  IMGENC1_Status enc_status;
  guint ret = IMGENC1_EFAIL;

  dyn_params = (IJPEGENC_DynamicParams *) ce_imgenc->codec_dyn_params;

  if (!dyn_params) {
    GST_WARNING_OBJECT (ce_imgenc, "couldn't set property");
    return;
  }

  switch (prop_id) {
    case PROP_ROTATION:
      dyn_params->rotation = g_value_get_enum (value);
      break;
    case PROP_DISABLE_EOI:
      dyn_params->disableEOI = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  /* Set dynamic parameters if needed */
  if (ce_imgenc->codec_handle) {
    enc_status.size = sizeof (IMGENC1_Status);
    enc_status.data.buf = NULL;
    ret = IMGENC1_control (ce_imgenc->codec_handle, XDM_SETPARAMS,
        (IMGENC1_DynamicParams *) dyn_params, &enc_status);
    if (IMGENC1_EOK != ret)
      GST_WARNING_OBJECT (ce_imgenc, "failed to set dynamic parameters, "
          "status error %x, %d", (guint) enc_status.extendedError, ret);
  }

  return;
}

/**
 * Gets custom properties from JPEG encoder
 */
static void
gst_ce_jpegenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCeImgEnc *ce_imgenc = GST_CE_IMGENC (object);
  IJPEGENC_DynamicParams *dyn_params;

  dyn_params = (IJPEGENC_DynamicParams *) ce_imgenc->codec_dyn_params;

  if (!dyn_params) {
    GST_WARNING_OBJECT (ce_imgenc, "couldn't get property");
    return;
  }

  switch (prop_id) {
    case PROP_ROTATION:
      g_value_set_enum (value, dyn_params->rotation);
      break;
    case PROP_DISABLE_EOI:
      g_value_set_boolean (value, dyn_params->disableEOI);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
