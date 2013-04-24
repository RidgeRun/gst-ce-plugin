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

#include <xdc/std.h>
#include <string.h>
#include <ti/sdo/codecs/jpegenc/ijpegenc.h>

#include "gstcejpegenc.h"

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_ce_jpegenc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "   format = (string) {NV12,UYVY},"
        "   framerate=(fraction)[ 0, 120], "
        "   width=(int)[ 128, 4080 ], " "   height=(int)[ 96, 4096 ]")
    );
/* *INDENT-ON* */

static GstStaticPadTemplate gst_ce_jpegenc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, "
        "   framerate=(fraction)[ 0, 120], "
        "   width=(int)[ 128, 4080 ], " "   height=(int)[ 96, 4096 ]")
    );

enum
{
  PROP_BASE = 0,
  PROP_ROTATION,
};

#define JPEG_DEFAULT_ROTATION 0

enum
{
  GST_CE_JPEGENC_ANGLE_0 = 0,
  GST_CE_JPEGENC_ANGLE_90 = 90,
  GST_CE_JPEGENC_ANGLE_180 = 180,
  GST_CE_JPEGENC_ANGLE_270 = 270,
};

#define GST_CE_JPEGENC_ROTATION_TYPE (gst_ceimgenc_rotation_get_type())
static GType
gst_ceimgenc_rotation_get_type (void)
{
  static GType rotation_type = 0;

  static const GEnumValue rotation_types[] = {
    {GST_CE_JPEGENC_ANGLE_0, "Angle 0 degrees", "Angle0"},
    {GST_CE_JPEGENC_ANGLE_90, "Angle 90 degrees", "Angle90"},
    {GST_CE_JPEGENC_ANGLE_180, "Angle 180 degrees", "Angle180"},
    {GST_CE_JPEGENC_ANGLE_270, "Angle 270 degrees", "Angle270"},
    {0, NULL, NULL}
  };

  if (!rotation_type) {
    rotation_type =
        g_enum_register_static ("GstCEJPEGEncRotation", rotation_types);
  }
  return rotation_type;
}

static void gst_ce_jpegenc_reset (GstCEImgEnc * ceimgenc);

static void gst_ce_jpegenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_ce_jpegenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

#define gst_ce_jpegenc_parent_class parent_class
G_DEFINE_TYPE (GstCEJPEGEnc, gst_ce_jpegenc, GST_TYPE_CEIMGENC);

static void
gst_ce_jpegenc_class_init (GstCEJPEGEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstCEImgEncClass *ceimgenc_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  ceimgenc_class = (GstCEImgEncClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_ce_jpegenc_set_property;
  gobject_class->get_property = gst_ce_jpegenc_get_property;

  g_object_class_install_property (gobject_class, PROP_ROTATION,
      g_param_spec_enum ("rotation", "Rotation",
          "Set the rotation angle", GST_CE_JPEGENC_ROTATION_TYPE,
          JPEG_DEFAULT_ROTATION, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* pad templates */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ce_jpegenc_sink_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ce_jpegenc_src_pad_template));

  gst_element_class_set_static_metadata (element_class,
      "CE JPEG image encoder", "Codec/Encoder/Image",
      "Encode image in JPEG format",
      "Carlos Gomez <carlos.gomez@ridgerun.com>");

  ceimgenc_class->codec_name = "jpegenc";
  ceimgenc_class->reset = gst_ce_jpegenc_reset;
}

static void
gst_ce_jpegenc_init (GstCEJPEGEnc * jpegenc)
{
  GstCEImgEnc *ceimgenc = (GstCEImgEnc *) (jpegenc);
  IJPEGENC_Params *jpeg_params = NULL;
  IJPEGENC_DynamicParams *jpeg_dyn_params = NULL;

  /* Alloc the params and set a default value */
  jpeg_params = g_malloc0 (sizeof (IJPEGENC_Params));
  if (!jpeg_params)
    goto fail_alloc;
  //*jpeg_params = IJPEGENC_PARAMS;

  jpeg_dyn_params = g_malloc0 (sizeof (IJPEGENC_DynamicParams));
  if (!jpeg_dyn_params)
    goto fail_alloc;
  //*jpeg_dyn_params = IJPEGENC_DYNAMICPARAMS;

  if (ceimgenc->codec_params) {
    GST_DEBUG ("codec params not NULL, copy and free them");
    jpeg_params->imgencParams = *ceimgenc->codec_params;
    g_free (ceimgenc->codec_params);
  }
  ceimgenc->codec_params = (IMGENC1_Params *) jpeg_params;

  if (ceimgenc->codec_dyn_params) {
    GST_DEBUG ("codec dynamic params not NULL, copy and free them");
    jpeg_dyn_params->imgencDynamicParams = *ceimgenc->codec_dyn_params;
    g_free (ceimgenc->codec_dyn_params);
  }
  ceimgenc->codec_dyn_params = (IMGENC1_DynamicParams *) jpeg_dyn_params;

  /* Add the extends params to the original params */
  ceimgenc->codec_params->size = sizeof (IJPEGENC_Params);
  ceimgenc->codec_dyn_params->size = sizeof (IJPEGENC_DynamicParams);

  gst_ce_jpegenc_reset (ceimgenc);

  return;

fail_alloc:
  {
    GST_WARNING_OBJECT (ceimgenc, "failed to allocate JPEG params");
    if (jpeg_params)
      g_free (jpeg_params);
    if (jpeg_dyn_params)
      g_free (jpeg_dyn_params);
    return;
  }
}

static void
gst_ce_jpegenc_reset (GstCEImgEnc * ceimgenc)
{
  GstCEJPEGEnc *jpegenc = (GstCEJPEGEnc *) (ceimgenc);
  IJPEGENC_Params *jpeg_params;
  IJPEGENC_DynamicParams *jpeg_dyn_params;

  if ((ceimgenc->codec_params->size != sizeof (IJPEGENC_Params)) ||
      (ceimgenc->codec_dyn_params->size != sizeof (IJPEGENC_DynamicParams)))
    return;

  jpeg_params = (IJPEGENC_Params *) ceimgenc->codec_params;
  jpeg_dyn_params = (IJPEGENC_DynamicParams *) ceimgenc->codec_dyn_params;

  GST_DEBUG ("setup JPEG parameters");
  /* Setting properties defaults */
  jpeg_params->halfBufCB = NULL;
  jpeg_params->halfBufCBarg = NULL;

  jpeg_dyn_params->disableEOI = 0;
  jpeg_dyn_params->rotation = JPEG_DEFAULT_ROTATION;
  jpeg_dyn_params->rstInterval = 84;
  jpeg_dyn_params->customQ = NULL;

  return;
}

static void
gst_ce_jpegenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCEImgEnc *ceimgenc = (GstCEImgEnc *) (object);
  // IJPEGENC_Params *params;
  IJPEGENC_DynamicParams *dyn_params;
  IMGENC1_Status enc_status;
  gboolean set_params = FALSE;
  guint ret;

  // params = (IJPEGENC_Params *) ceimgenc->codec_params;
  dyn_params = (IJPEGENC_DynamicParams *) ceimgenc->codec_dyn_params;

  if (!dyn_params) {            //(!params) || (!dyn_params)
    GST_WARNING_OBJECT (ceimgenc, "couldn't set property");
    return;
  }

  switch (prop_id) {
    case PROP_ROTATION:
      dyn_params->rotation = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  /* Set dynamic parameters if needed */
  if (set_params && ceimgenc->codec_handle) {
    enc_status.size = sizeof (IMGENC1_Status);
    enc_status.data.buf = NULL;
    ret = IMGENC1_control (ceimgenc->codec_handle, XDM_SETPARAMS,
        (IMGENC1_DynamicParams *) dyn_params, &enc_status);
    if (ret != IMGENC1_EOK)
      GST_WARNING_OBJECT (ceimgenc, "failed to set dynamic parameters, "
          "status error %x, %d", (guint) enc_status.extendedError, ret);
  }

  return;
  //fail_static_prop:
  //GST_WARNING_OBJECT (ceimgenc, "can't set static property when "
  //    "the codec is already configured");
}

static void
gst_ce_jpegenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCEImgEnc *ceimgenc = (GstCEImgEnc *) (object);
  // IJPEGENC_Params *params;
  IJPEGENC_DynamicParams *dyn_params;

  // params = (IJPEGENC_Params *) ceimgenc->codec_params;
  dyn_params = (IJPEGENC_DynamicParams *) ceimgenc->codec_dyn_params;

  if (!dyn_params) {            //(!params) || (!dyn_params)
    GST_WARNING_OBJECT (ceimgenc, "couldn't set property");
    return;
  }

  switch (prop_id) {
    case PROP_ROTATION:
      g_value_set_enum (value, dyn_params->rotation);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
