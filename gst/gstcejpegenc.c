/*
 * gstcejpegenc.c
 *
 * Original Author:
 *     Carlos Gomez, RidgeRun
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

#include <xdc/std.h>
#include <string.h>
#include <ti/sdo/codecs/jpegenc/ijpegenc.h>

#include "gstcejpegenc.h"
#include "gstceimgenc.h"

#define MAX=2147483647

enum
{
  PROP_QUALITY,
  PROP_SMOOTHING,
  PROP_IDCT_METHOD,
  PROP_DISABLE_EOI,
  PROP_ROTATION
};

#define JPEG_DEFAULT_QUALITY 85
#define JPEG_DEFAULT_SMOOTHING 0
#define JPEG_DEFAULT_IDCT_METHOD JDCT_FASTEST
#define JPEG_DEFAULT_DISABLE_EOI XDM_DEFAULT
#define JPEG_DEFAULT_ROTATION 0

GstStaticCaps gst_ce_jpegenc_sink_caps = GST_STATIC_CAPS(
        ("video/x-raw, "
	 "format=(string){I420, YV12, YUY2, UYVY, Y41B, Y42B, YVYU, Y444, RGB, BGR, RGBx, xRGB, BGRx, xBGR, GRAY8}"
            "width=(int)[ 1, MAX ], "
            "height=(int)[ 1, MAX ], "
            "framerate=(fraction)[ 0/1, MAX/1 ]"
        )
);

GstStaticCaps gst_ce_jpegenc_src_caps = GST_STATIC_CAPS(
        ("image/jpeg, "
            "width=(int)[ 16, 65535 ], "
            "height=(int)[ 16, 65535 ], "
            "framerate=(fraction)[ 0/1, MAX/1 ]"
        )
);

static void
gst_ce_jpegenc_install_properties (GObjectClass * gobject_class, guint base)
{
  g_object_class_install_property (gobject_class, base + PROP_QUALITY,
      g_param_spec_int ("quality", "Quality", "Quality of encoding",
          0, 100, JPEG_DEFAULT_QUALITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, base + PROP_SMOOTHING,
      g_param_spec_int ("smoothing", "Smoothing", "Smoothing factor",
          0, 100, JPEG_DEFAULT_SMOOTHING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, base + PROP_IDCT_METHOD,
      g_param_spec_enum ("idct-method", "IDCT Method",
          "The IDCT algorithm to use", GST_TYPE_IDCT_METHOD,
          JPEG_DEFAULT_IDCT_METHOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, base + PROP_DISABLE_EOI,
     g_param_spec_enum ("disableEOI", "Disable EOI",
         "Disable End of Image", GST_TYPE_DISABLE_EOI,
         JPEG_DEFAULT_DISABLE_EOI,
         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
 g_object_class_install_property (gobject_class, base + PROP_ROTATION,
      g_param_spec_enum ("rotation", "Rotation",
          "Set the rotation angle (0,90,180,270)", GST_TYPE_ROTATION,
          JPEG_DEFAULT_ROTATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_ce_jpegenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec, guint base)
{
  GstCEImgEnc *ceimgenc = (GstCEImgEnc *) (object);
  IJPEGENC_DynamicParams *dyn_params;
  guint prop_jpeg_id;

  dyn_params = (IJPEGENC_DynamicParams *) ceimgenc->codec_dyn_params;
  prop_jpeg_id = prop_id - base;
 
  switch (prop_jpeg_id) {
    case PROP_QUALITY:
      ceimgenc->quality = g_value_get_int (value);
      break;
    case PROP_SMOOTHING:
      ceimgenc->smoothing = g_value_get_int (value);
      break;
    case PROP_IDCT_METHOD:
      ceimgenc->idct_method = g_value_get_enum (value);
      break;
    case PROP_DISABLE_EOI:
      dyn_params->disableEOI = g_value_get_enum (value);
      break;
    case PROP_ROTATION:
      dyn_params->rotation = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ce_jpegenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec, guint base)
{
  GstCEImgEnc *ceimgenc = (GstCEImgEnc *) (object);
  IJPEGENC_DynamicParams *dyn_params;
  guint prop_jpeg_id;

  dyn_params = (IJPEGENC_DynamicParams *) ceimgenc->codec_dyn_params;
  prop_jpeg_id = prop_id - base;

  switch (prop_jpeg_id) {
    case PROP_QUALITY: 
      g_value_set_int (value, ceimgenc->quality);
      break;
    case PROP_SMOOTHING:
      g_value_set_int (value, ceimgenc->smoothing);
      break;
    case PROP_IDCT_METHOD:
      g_value_set_enum (value, ceimgenc->idct_method);
      break;
    case PROP_DISABLE_EOI:
      g_value_set_enum (value, params->disableEOI);
      break;
    case PROP_ROTATION:
      g_value_set_enum (value, params->rotation);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ce_jpegenc_setup (GObject * object)
{
  GstCEImgEnc *ceimgenc = (GstCEImgEnc *) (object);
  // jpegPrivateData *jpegenc; ??
  IJPEGENC_Params *jpeg_params = NULL;
  IJPEGENC_DynamicParams *jpeg_dyn_params = NULL;

  GST_DEBUG ("setup JPEG parameters");
  /* Alloc the params and set a default value */
  jpeg_params = g_malloc0 (sizeof (IJPEGENC_Params));
  if (!jpeg_params)
    goto fail_alloc;
  *jpeg_params = IJPEGENC_PARAMS;

  jpeg_dyn_params = g_malloc0 (sizeof (IJPEGENC_DynamicParams));
  if (!jpeg_dyn_params)
    goto fail_alloc;
  *jpeg_dyn_params = HJPEGENC_TI_IJPEGENC_DYNAMICPARAMS;

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
  GST_DEBUG_OBJECT (ceimgenc, "allocating JPEG private data");
  if (ceimgenc->codec_private)
    g_free (ceimgenc->codec_private);
  
  /* Setting properties defaults */
  ceimgenc->quality = JPEG_DEFAULT_QUALITY;
  ceimgenc->smoothing = JPEG_DEFAULT_SMOOTHING;
  ceimgenc->idct_method = JPEG_DEFAULT_IDCT_METHOD;
  jpeg_dyn_params->disableEOI = JPEG_DEFAULT_DISABLE_EOI;
  jpeg_dyn_params->rotation = JPEG_DEFAULT_ROTATION;

  return;

fail_alloc:
  {
    GST_WARNING_OBJECT (ceimgenc, "failed to allocate JPEG params");
    if (jpeg_params)
      g_free (jpeg_params);
    if (jpeg_dyn_params)
      g_free (jpeg_params);
    return;
  }
}


GstCECodecData gst_ce_jpegenc = {
  .name = "jpegenc",
  .long_name = "JPEG",
  .src_caps = &gst_ce_jpegenc_src_caps,
  .sink_caps = &gst_ce_jpegenc_sink_caps,
  .install_properties = gst_ce_jpegenc_install_properties,
  .set_property = gst_ce_jpegenc_set_property,
  .get_property = gst_ce_jpegenc_get_property,
  .setup = gst_ce_jpegenc_setup, 
};
