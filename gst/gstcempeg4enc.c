/*
 * gstcempeg4enc.c
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
#include <ti/sdo/codecs/mpeg4enc_hdvicp/imp4venc.h>

#include "gstcempeg4enc.h"

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_ce_mpeg4enc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "   format = (string) {NV12},"
        "   framerate=(fraction)[ 0, 120], "
        "   width=(int)[ 64, 1920 ], " 
        "   height=(int)[ 64, 1088 ]")
    );

static GstStaticPadTemplate gst_ce_mpeg4enc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "   mpegversion=(int) 4, "
        "   systemstream=(boolean)false, "
        "   framerate=(fraction)[ 0, 120], "
        "   width=(int)[ 64, 1920 ], " 
        "   height=(int)[ 64, 1088 ]")
    );

/* *INDENT-ON* */
enum
{
  PROP_0,
  PROP_LEVEL,
  PROP_USE_VOS,
  PROP_USE_GOV,
  PROP_MOTION_ALG,
  PROP_ENC_QUALITY,
  PROP_RC_ALGO,
};

#define PROP_LEVEL_DEFAULT          IMP4HDVICPENC_SP_LEVEL_5
#define PROP_USE_VOS_DEFAULT        TRUE
#define PROP_USE_GOV_DEFAULT        FALSE
#define PROP_MOTION_ALG_DEFAULT     0
#define PROP_ENC_QUALITY_DEFAULT    0
#define PROP_RC_ALGO_DEFAULT        IMP4HDVICPENC_RC_DEFAULT

#define GST_CE_MPEG4ENC_LEVEL_TYPE (gst_ce_mpeg4enc_level_get_type())
static GType
gst_ce_mpeg4enc_level_get_type (void)
{
  static GType level_type = 0;

  static const GEnumValue level_types[] = {
    {IMP4HDVICPENC_SP_LEVEL_0, "Level 0", "0"},
    {IMP4HDVICPENC_SP_LEVEL_0B, "Level 0b", "0b"},
    {IMP4HDVICPENC_SP_LEVEL_1, "Level 1", "1"},
    {IMP4HDVICPENC_SP_LEVEL_2, "Level 2", "2"},
    {IMP4HDVICPENC_SP_LEVEL_3, "Level 3", "3"},
    {IMP4HDVICPENC_SP_LEVEL_4A, "Level 4a", "4a"},
    {IMP4HDVICPENC_SP_LEVEL_5, "Level 5", "5"},
    {0, NULL, NULL}
  };

  if (!level_type) {
    level_type = g_enum_register_static ("GstCeMpeg4EncLevel", level_types);
  }
  return level_type;
}

#define GST_CE_MPEG4ENC_MOTION_ALG_TYPE (gst_ce_mpeg4enc_motion_alg_get_type())
static GType
gst_ce_mpeg4enc_motion_alg_get_type (void)
{
  static GType motion_alg_type = 0;

  static const GEnumValue motion_alg_types[] = {
    {0, "Normal search algorithm", "normal"},
    {1, "Low power search algorithm", "low"},
    {0, NULL, NULL}
  };

  if (!motion_alg_type) {
    motion_alg_type =
        g_enum_register_static ("GstCeMpeg4EncMotionAlg", motion_alg_types);
  }
  return motion_alg_type;
}

#define GST_CE_MPEG4ENC_ENC_QUALITY_TYPE (gst_ce_mpeg4enc_enc_quality_get_type())
static GType
gst_ce_mpeg4enc_enc_quality_get_type (void)
{
  static GType enc_quality_type = 0;

  static const GEnumValue enc_quality_types[] = {
    {0, "High quality", "high"},
    {1, "Standard quality", "standard"},
    {0, NULL, NULL}
  };

  if (!enc_quality_type) {
    enc_quality_type =
        g_enum_register_static ("GstCeMpeg4EncQualityEst", enc_quality_types);
  }
  return enc_quality_type;
}

#define GST_CE_MPEG4ENC_RC_ALGO_TYPE (gst_ce_mpeg4enc_rc_algo_get_type())
static GType
gst_ce_mpeg4enc_rc_algo_get_type (void)
{
  static GType rc_algo_type = 0;

  static const GEnumValue rc_algo_types[] = {
    {IMP4HDVICPENC_RC_NONE, "No rate control is used", "none"},
    {IMP4HDVICPENC_RC_CBR,
        "Constant Bit-Rate(CBR) control for video conferencing", "cbr"},
    {IMP4HDVICPENC_RC_VBR,
        "Variable Bit-Rate(VBR) control for local storage recording", "vbr"},
    {0, NULL, NULL}
  };

  if (!rc_algo_type) {
    rc_algo_type =
        g_enum_register_static ("GstCeMpeg4EncRcAlgo", rc_algo_types);
  }
  return rc_algo_type;
}

static void gst_ce_mpeg4enc_reset (GstCeVidEnc * ce_videnc);
static gboolean gst_ce_mpeg4enc_set_src_caps (GstCeVidEnc * ce_videnc,
    GstCaps ** caps, GstBuffer ** codec_data);
static gboolean gst_ce_mpeg4enc_post_process (GstCeVidEnc * ce_videnc,
    GstBuffer * buffer);

static void gst_ce_mpeg4enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_ce_mpeg4enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

#define gst_ce_mpeg4enc_parent_class parent_class
G_DEFINE_TYPE (GstCeMpeg4Enc, gst_ce_mpeg4enc, GST_TYPE_CEVIDENC);

static void
gst_ce_mpeg4enc_class_init (GstCeMpeg4EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstCeVidEncClass *ce_videnc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  ce_videnc_class = GST_CEVIDENC_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_ce_mpeg4enc_set_property;
  gobject_class->get_property = gst_ce_mpeg4enc_get_property;

  g_object_class_install_property (gobject_class, PROP_LEVEL,
      g_param_spec_enum ("level", "Level",
          "Profile level indication for the encoder",
          GST_CE_MPEG4ENC_LEVEL_TYPE, PROP_LEVEL_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_USE_VOS,
      g_param_spec_boolean ("use-vos",
          "Use VOS",
          "Use MPEG-4 Visual sequence header",
          PROP_USE_VOS_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_USE_GOV,
      g_param_spec_boolean ("use-gov",
          "Use GOV",
          "Use MPEG-4 GOV header", PROP_USE_GOV_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_MOTION_ALG,
      g_param_spec_enum ("motion-alg", "Motion Estimation algorithm",
          "Motion Estimation(ME) algorithm type to be used by the encoder. Low power ME search algorithm has reduced search points and may reduce the quality",
          GST_CE_MPEG4ENC_MOTION_ALG_TYPE, PROP_MOTION_ALG_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ENC_QUALITY,
      g_param_spec_enum ("enc-quality", "Encoding quality",
          "Quality mode for encoding. Using standard quality mode may reduce the quality but performance is improved",
          GST_CE_MPEG4ENC_ENC_QUALITY_TYPE, PROP_ENC_QUALITY_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RC_ALGO,
      g_param_spec_enum ("rc-algo", "Rate control Algorithm",
          "Rate Control algorithm to be used.",
          GST_CE_MPEG4ENC_RC_ALGO_TYPE,
          PROP_RC_ALGO_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* pad templates */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ce_mpeg4enc_sink_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ce_mpeg4enc_src_pad_template));

  gst_element_class_set_static_metadata (element_class,
      "CE MPEG-4 HDVICP video encoder", "Codec/Encoder/Video",
      "Encode video in MPEG-4 format",
      "Melissa Montero <melissa.montero@ridgerun.com>");

  ce_videnc_class->codec_name = "mpeg4enc";
  ce_videnc_class->reset = gst_ce_mpeg4enc_reset;
  ce_videnc_class->set_src_caps = gst_ce_mpeg4enc_set_src_caps;
  //~ ce_videnc_class->post_process = gst_ce_mpeg4enc_post_process;
  /*$
   * TODO
   * Do we want to set mpeg4 specific debug?
   */
  //~ GST_DEBUG_CATEGORY_INIT (mpeg4enc_debug, "ce_mpeg4enc", 0,
  //~ "H.264 encoding element");
}

static void
gst_ce_mpeg4enc_init (GstCeMpeg4Enc * mpeg4enc)
{
  GstCeVidEnc *ce_videnc = GST_CEVIDENC (mpeg4enc);
  IMP4HDVICPENC_Params *mpeg4enc_params = NULL;
  IMP4HDVICPENC_DynamicParams *mpeg4enc_dyn_params = NULL;

  GST_DEBUG_OBJECT (mpeg4enc, "setup MPEG-4 parameters");

  /* Alloc the params and set a default value */
  mpeg4enc_params = g_malloc0 (sizeof (IMP4HDVICPENC_Params));
  if (!mpeg4enc_params)
    goto fail_alloc;
  *mpeg4enc_params = IMPEG4VENC_PARAMS;

  mpeg4enc_dyn_params = g_malloc0 (sizeof (IMP4HDVICPENC_DynamicParams));
  if (!mpeg4enc_dyn_params)
    goto fail_alloc;

  if (ce_videnc->codec_params) {
    GST_DEBUG_OBJECT (mpeg4enc, "codec params not NULL, copy and free them");
    mpeg4enc_params->videncParams = *ce_videnc->codec_params;
    g_free (ce_videnc->codec_params);
  }
  ce_videnc->codec_params = (VIDENC1_Params *) mpeg4enc_params;

  if (ce_videnc->codec_dyn_params) {
    GST_DEBUG_OBJECT (mpeg4enc,
        "codec dynamic params not NULL, copy and free them");
    mpeg4enc_dyn_params->videncDynamicParams = *ce_videnc->codec_dyn_params;
    g_free (ce_videnc->codec_dyn_params);
  }
  ce_videnc->codec_dyn_params = (VIDENC1_DynamicParams *) mpeg4enc_dyn_params;

  /* Add the extends params to the original params */
  ce_videnc->codec_params->size = sizeof (IMP4HDVICPENC_Params);
  ce_videnc->codec_dyn_params->size = sizeof (IMP4HDVICPENC_DynamicParams);

  gst_ce_mpeg4enc_reset (ce_videnc);
  return;

fail_alloc:
  {
    GST_WARNING_OBJECT (ce_videnc, "failed to allocate MPEG-4 params");
    if (mpeg4enc_params)
      g_free (mpeg4enc_params);
    if (mpeg4enc_dyn_params)
      g_free (mpeg4enc_params);
    return;
  }
  return;

}

static gboolean
gst_ce_mpeg4enc_get_codec_data (GstCeMpeg4Enc * mpeg4enc,
    GstBuffer ** codec_data)
{
  GstBuffer *buf;
  GstMapInfo info;
  guint8 *header, *buffer;
  gint codec_data_size = 0;
  gint32 state, start_code;
  gint i;

  GST_DEBUG_OBJECT (mpeg4enc, "generating codec data..");

  if (!gst_ce_videnc_get_header (GST_CEVIDENC (mpeg4enc), &buf,
          &mpeg4enc->header_size))
    return FALSE;

  /*Get pointer to the header data */
  if (!gst_buffer_map (buf, &info, GST_MAP_READ))
    return FALSE;

  header = info.data;

  GST_LOG ("fetching header...");
  GST_MEMDUMP ("Header", header, mpeg4enc->header_size);

  /* Search the object layer start code */
  start_code = 0x00000120;
  state = ~(start_code);
  for (i = 0; i < mpeg4enc->header_size; ++i) {
    state = ((state << 8) | header[i]);
    if (state == start_code)
      break;
  }
  i++;
  /* Search next start code */
  start_code = 0x00000100;
  state = ~(start_code);
  for (; i < mpeg4enc->header_size; ++i) {
    state = (state | header[i]) << 8;
    if (state == start_code)
      break;
  }

  if (i) {
    /* We found a codec data */
    codec_data_size = i;
    buffer = g_malloc (codec_data_size);
    memcpy (buffer, header, codec_data_size);
    GST_MEMDUMP ("Codec data", buffer, codec_data_size);
    *codec_data = gst_buffer_new_wrapped (buffer, codec_data_size);
  }

  gst_buffer_unmap (buf, &info);
  gst_buffer_unref (buf);

  return TRUE;
}

static gboolean
gst_ce_mpeg4enc_set_src_caps (GstCeVidEnc * ce_videnc, GstCaps ** caps,
    GstBuffer ** codec_data)
{
  GstCeMpeg4Enc *mpeg4enc = GST_CE_MPEG4ENC (ce_videnc);

  GST_DEBUG_OBJECT (mpeg4enc, "setting MPEG-4 caps");

  return gst_ce_mpeg4enc_get_codec_data (mpeg4enc, codec_data);
}

static void
gst_ce_mpeg4enc_reset (GstCeVidEnc * ce_videnc)
{
  GstCeMpeg4Enc *mpeg4enc = GST_CE_MPEG4ENC (ce_videnc);
  IMP4HDVICPENC_Params *mpeg4enc_params = NULL;
  IMP4HDVICPENC_DynamicParams *mpeg4enc_dyn_params = NULL;

  GST_DEBUG_OBJECT (mpeg4enc, "MPEG-4 reset");

  if ((ce_videnc->codec_params->size != sizeof (IMP4HDVICPENC_Params)) ||
      (ce_videnc->codec_dyn_params->size !=
          sizeof (IMP4HDVICPENC_DynamicParams)))
    return;

  mpeg4enc_params = (IMP4HDVICPENC_Params *) ce_videnc->codec_params;
  mpeg4enc_dyn_params =
      (IMP4HDVICPENC_DynamicParams *) ce_videnc->codec_dyn_params;

  /* Setting properties defaults */
  mpeg4enc_params->levelIdc = PROP_LEVEL_DEFAULT;
  mpeg4enc_params->useVOS = PROP_USE_VOS_DEFAULT;
  mpeg4enc_params->useGOV = PROP_USE_GOV_DEFAULT;
  mpeg4enc_params->ME_Type = PROP_MOTION_ALG_DEFAULT;
  mpeg4enc_params->EncQuality_mode = PROP_ENC_QUALITY_DEFAULT;
  mpeg4enc_dyn_params->RcAlgo = PROP_RC_ALGO_DEFAULT;

  /* Setting defaults to dynamic params */
  mpeg4enc_dyn_params->Four_MV_mode = 0;
  mpeg4enc_dyn_params->PacketSize = 0;
  mpeg4enc_dyn_params->qpIntra = 8;
  mpeg4enc_dyn_params->qpInter = 8;
  mpeg4enc_dyn_params->airRate = 0;
  mpeg4enc_dyn_params->useHEC = 0;
  mpeg4enc_dyn_params->useGOBSync = 0;
  mpeg4enc_dyn_params->QPMax = 31;
  mpeg4enc_dyn_params->QPMin = 2;
  mpeg4enc_dyn_params->maxDelay = 1000;
  mpeg4enc_dyn_params->qpInit = 8;
  mpeg4enc_dyn_params->PerceptualRC = 0;
  mpeg4enc_dyn_params->reset_vIMCOP_every_frame = 1;
  mpeg4enc_dyn_params->mvSADoutFlag = 0;

  return;
}

static void
gst_ce_mpeg4enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCeVidEnc *ce_videnc = GST_CEVIDENC (object);
  IMP4HDVICPENC_Params *params = NULL;
  IMP4HDVICPENC_DynamicParams *dyn_params = NULL;
  VIDENC1_Status enc_status;
  gboolean set_dyn_params = FALSE;
  guint ret;

  params = (IMP4HDVICPENC_Params *) ce_videnc->codec_params;
  dyn_params = (IMP4HDVICPENC_DynamicParams *) ce_videnc->codec_dyn_params;

  if ((!params) || (!dyn_params)) {
    GST_WARNING_OBJECT (ce_videnc, "couldn't set property");
    return;
  }

  /* Setting static property */
  if (!ce_videnc->codec_handle) {
    switch (prop_id) {
      case PROP_LEVEL:
        params->levelIdc = g_value_get_enum (value);
        break;
      case PROP_USE_VOS:
        params->useVOS = g_value_get_boolean (value) ? 1 : 0;
        break;
      case PROP_USE_GOV:
        params->useGOV = g_value_get_boolean (value) ? 1 : 0;
        break;
      case PROP_MOTION_ALG:
        params->ME_Type = g_value_get_enum (value);
        break;
      case PROP_ENC_QUALITY:
        params->EncQuality_mode = g_value_get_enum (value);
        break;
      default:
        set_dyn_params = TRUE;
        break;
    }
  } else {
    switch (prop_id) {
      case PROP_LEVEL:
      case PROP_USE_VOS:
      case PROP_USE_GOV:
      case PROP_MOTION_ALG:
      case PROP_ENC_QUALITY:
        goto fail_static_prop;
      default:
        set_dyn_params = TRUE;
        break;
    }
  }

  /* Setting dynamic property */
  if (set_dyn_params) {
    switch (prop_id) {
      case PROP_RC_ALGO:
        dyn_params->RcAlgo = g_value_get_enum (value);
        break;
      default:
        set_dyn_params = FALSE;
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
    /* Set dynamic parameters if needed */
    if (set_dyn_params && ce_videnc->codec_handle) {
      enc_status.size = sizeof (VIDENC1_Status);
      enc_status.data.buf = NULL;
      ret = VIDENC1_control (ce_videnc->codec_handle, XDM_SETPARAMS,
          (VIDENC1_DynamicParams *) dyn_params, &enc_status);
      if (ret != VIDENC1_EOK)
        GST_WARNING_OBJECT (ce_videnc, "failed to set dynamic parameters, "
            "status error %x, %d", (guint) enc_status.extendedError, ret);
    }
  }
  return;

fail_static_prop:
  GST_WARNING_OBJECT (ce_videnc, "can't set static property when "
      "the codec is already configured");

  return;
}

static void
gst_ce_mpeg4enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCeVidEnc *ce_videnc = GST_CEVIDENC (object);
  IMP4HDVICPENC_Params *params = NULL;
  IMP4HDVICPENC_DynamicParams *dyn_params = NULL;

  params = (IMP4HDVICPENC_Params *) ce_videnc->codec_params;
  dyn_params = (IMP4HDVICPENC_DynamicParams *) ce_videnc->codec_dyn_params;

  if ((!params) || (!dyn_params)) {
    GST_WARNING_OBJECT (ce_videnc, "couldn't set property");
    return;
  }

  switch (prop_id) {
    case PROP_LEVEL:
      g_value_set_enum (value, params->levelIdc);
      break;
    case PROP_USE_VOS:
      g_value_set_boolean (value, params->useVOS);
      break;
    case PROP_USE_GOV:
      g_value_set_boolean (value, params->useGOV);
      break;
    case PROP_MOTION_ALG:
      g_value_set_enum (value, params->ME_Type);
      break;
    case PROP_ENC_QUALITY:
      g_value_set_enum (value, params->EncQuality_mode);
      break;
    case PROP_RC_ALGO:
      g_value_set_enum (value, dyn_params->RcAlgo);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
