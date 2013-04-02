/*
 * gstceh264enc.c
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
#include <ti/sdo/codecs/h264enc/ih264venc.h>

#include "gstceh264enc.h"

GstStaticCaps gst_ce_h264enc_sink_caps = GST_STATIC_CAPS ("video/x-raw, "
    "   format = (string) NV12,"
    "   framerate=(fraction)[ 0, 120], "
    "   width=(int)[ 128, 4080 ], " "   height=(int)[ 96, 4096 ]");

GstStaticCaps gst_ce_h264enc_src_caps = GST_STATIC_CAPS ("video/x-h264, "
    "   framerate=(fraction)[ 0, 120], "
    "   width=(int)[ 128, 4080 ], "
    "   height=(int)[ 96, 4096 ],"
    "   stream-format = (string) { avc, byte-stream }");

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_ce_h264enc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "   format = (string) NV12,"
        "   framerate=(fraction)[ 0, 120], "
        "   width=(int)[ 128, 4080 ], " "   height=(int)[ 96, 4096 ]")
    );
/* *INDENT-ON* */

static GstStaticPadTemplate gst_ce_h264enc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "   framerate=(fraction)[ 0, 120], "
        "   width=(int)[ 128, 4080 ], "
        "   height=(int)[ 96, 4096 ],"
        "   stream-format = (string) { avc, byte-stream }")
    );

#define NAL_LENGTH 4

enum
{
  PROP_BASE = 0,
  PROP_BYTESTREAM,
  PROP_HEADERS,
  PROP_SINGLE_NALU,
  PROP_PROFILE,
  PROP_LEVEL,
  PROP_ENTROPYMODE,
  PROP_T8X8INTRA,
  PROP_T8X8INTER,
  PROP_ENCQUALITY,
  PROP_ENABLETCM,
  PROP_DDRBUF,
  PROP_NTEMPLAYERS,
  PROP_SVCSYNTAXEN,
  PROP_SEQSCALING,
  PROP_QPINTRA,
  PROP_QPINTER,
  PROP_RCALGO,
  PROP_AIRRATE,
  PROP_IDRINTERVAL,
};

enum
{
  GST_CE_H264ENC_STREAM_FORMAT_AVC,
  GST_CE_H264ENC_STREAM_FORMAT_BYTE_STREAM,
  GST_CE_H264ENC_STREAM_FORMAT_FROM_PROPERTY
};

enum
{
  GST_H264_NAL_UNKNOWN = 0,
  GST_H264_NAL_SLICE = 1,
  GST_H264_NAL_SLICE_IDR = 5,
  GST_H264_NAL_SEI = 6,
  GST_H264_NAL_SPS = 7,
  GST_H264_NAL_PPS = 8,
};


typedef struct
{
  gint current_stream_format;
  gboolean byte_stream;
  gboolean headers;
  gboolean single_nalu;
  gint header_size;

} h264PrivateData;

typedef struct
{
  gint type;
  gint index;
  gint size;
} nalUnit;

#define PROP_BYTESTREAM_DEFAULT           FALSE
#define PROP_HEADERS_DEFAULT              FALSE
#define PROP_SINGLE_NALU_DEFAULT          FALSE
#define PROP_PROFILE_DEFAULT              100
#define PROP_LEVEL_DEFAULT                IH264VENC_LEVEL_40
#define PROP_ENTROPYMODE_DEFAULT          1
#define PROP_T8X8INTRA_DEFAULT            TRUE
#define PROP_T8X8INTER_DEFAULT            FALSE
#define PROP_ENCQUALITY_DEFAULT           XDM_HIGH_SPEED
#define PROP_ENABLETCM_DEFAULT            FALSE
#define PROP_DDRBUF_DEFAULT               FALSE
#define PROP_NTEMPLAYERS_DEFAULT          0
#define PROP_SVCSYNTAXEN_DEFAULT          0
#define PROP_SEQSCALING_DEFAULT           1
#define PROP_QPINTRA_DEFAULT              28
#define PROP_QPINTER_DEFAULT              28
#define PROP_RCALGO_DEFAULT               1
#define PROP_AIRRATE_DEFAULT              0
#define PROP_IDRINTERVAL_DEFAULT          0

enum
{
  GST_CE_H264ENC_PROFILE_BASE = 66,
  GST_CE_H264ENC_PROFILE_MAIN = 77,
  GST_CE_H264ENC_PROFILE_HIGH = 100,
};

#define GST_CE_H264ENC_PROFILE_TYPE (gst_cevidenc_profile_get_type())
static GType
gst_cevidenc_profile_get_type (void)
{
  static GType profile_type = 0;

  static const GEnumValue profile_types[] = {
    {GST_CE_H264ENC_PROFILE_BASE, "Base line", "base"},
    {GST_CE_H264ENC_PROFILE_MAIN, "Main profile", "main"},
    {GST_CE_H264ENC_PROFILE_HIGH, "High profile", "high"},
    {0, NULL, NULL}
  };

  if (!profile_type) {
    profile_type =
        g_enum_register_static ("GstCEH264EncProfile", profile_types);
  }
  return profile_type;
}

#define GST_CE_H264ENC_LEVEL_TYPE (gst_cevidenc_level_get_type())
static GType
gst_cevidenc_level_get_type (void)
{
  static GType level_type = 0;

  static const GEnumValue level_types[] = {
    {IH264VENC_LEVEL_10, "Level 1.0", "1.0"},
    {IH264VENC_LEVEL_1b, "Level 1.b", "1.b"},
    {IH264VENC_LEVEL_11, "Level 1.1", "1.1"},
    {IH264VENC_LEVEL_12, "Level 1.2", "1.2"},
    {IH264VENC_LEVEL_13, "Level 1.3", "1.3"},
    {IH264VENC_LEVEL_20, "Level 2.0", "2.0"},
    {IH264VENC_LEVEL_21, "Level 2.1", "2.1"},
    {IH264VENC_LEVEL_22, "Level 2.2", "2.2"},
    {IH264VENC_LEVEL_30, "Level 3.0", "3.0"},
    {IH264VENC_LEVEL_31, "Level 3.1", "3.1"},
    {IH264VENC_LEVEL_32, "Level 3.2", "3.2"},
    {IH264VENC_LEVEL_40, "Level 4.0", "4.0"},
    {IH264VENC_LEVEL_41, "Level 4.1", "4.1"},
    {IH264VENC_LEVEL_42, "Level 4.2", "4.2"},
    {IH264VENC_LEVEL_50, "Level 5.0", "5.0"},
    {IH264VENC_LEVEL_51, "Level 5.1", "5.1"},
    {0, NULL, NULL}
  };

  if (!level_type) {
    level_type = g_enum_register_static ("GstCEH264EncLevel", level_types);
  }
  return level_type;
}

enum
{
  GST_CE_H264ENC_ENTROPY_CAVLC = 0,
  GST_CE_H264ENC_ENTROPY_CABAC = 1,
};

#define GST_CE_H264ENC_ENTROPY_TYPE (gst_cevidenc_entropy_get_type())
static GType
gst_cevidenc_entropy_get_type (void)
{
  static GType entropy_type = 0;

  static const GEnumValue entropy_types[] = {
    {GST_CE_H264ENC_ENTROPY_CAVLC, "CAVLC", "cavlc"},
    {GST_CE_H264ENC_ENTROPY_CABAC, "CABAC", "cabac"},
    {0, NULL, NULL}
  };

  if (!entropy_type) {
    entropy_type =
        g_enum_register_static ("GstCEH264EncEntropy", entropy_types);
  }
  return entropy_type;
}

enum
{
  GST_CE_H264ENC_SEQSCALING_DISABLE = 0,
  GST_CE_H264ENC_SEQSCALING_AUTO,
  GST_CE_H264ENC_SEQSCALING_LOW,
  GST_CE_H264ENC_SEQSCALING_MODERATE,
};

#define GST_CE_H264ENC_SEQSCALING_TYPE (gst_cevidenc_seqscaling_get_type())
static GType
gst_cevidenc_seqscaling_get_type (void)
{
  static GType seqscaling_type = 0;

  static const GEnumValue seqscaling_types[] = {
    {GST_CE_H264ENC_SEQSCALING_DISABLE, "Disable", "disable"},
    {GST_CE_H264ENC_SEQSCALING_AUTO, "Auto", "auto"},
    {GST_CE_H264ENC_SEQSCALING_LOW, "Low", "low"},
    {GST_CE_H264ENC_SEQSCALING_MODERATE, "Moderate", "moderate"},
    {0, NULL, NULL}
  };

  if (!seqscaling_type) {
    seqscaling_type =
        g_enum_register_static ("GstCEH264EncSeqScaling", seqscaling_types);
  }
  return seqscaling_type;
}

#define GST_CE_H264ENC_QUALITY_TYPE (gst_cevidenc_quality_get_type())
static GType
gst_cevidenc_quality_get_type (void)
{
  static GType quality_type = 0;

  static const GEnumValue quality_types[] = {
    {0, "version 1.1, backward compatible mode", "backward"},
    {XDM_HIGH_QUALITY, "High quality mode", "quality"},
    {XDM_HIGH_SPEED, "High speed mode", "speed"},
    {0, NULL, NULL}
  };

  if (!quality_type) {
    quality_type = g_enum_register_static ("GstCEVidEncQuality", quality_types);
  }
  return quality_type;
}

enum
{
  GST_CE_H264ENC_LAYERS_ONE = 0,
  GST_CE_H264ENC_LAYERS_TWO,
  GST_CE_H264ENC_LAYERS_THREE,
  GST_CE_H264ENC_LAYERS_FOUR,
  GST_CE_H264ENC_LAYERS_ALL = 255,
};

#define GST_CE_H264ENC_LAYERS_TYPE (gst_cevidenc_layers_get_type())
static GType
gst_cevidenc_layers_get_type (void)
{
  static GType layers_type = 0;

  static const GEnumValue layers_types[] = {
    {GST_CE_H264ENC_LAYERS_ONE,
        "One layer (Stream with frame rate: F)", "one"},
    {GST_CE_H264ENC_LAYERS_TWO,
        "Two layers (Stream with frame rate: F, F/2)", "two"},
    {GST_CE_H264ENC_LAYERS_THREE,
        "Three layers (Stream with frame rate: F, F/2, F/8)", "three"},
    {GST_CE_H264ENC_LAYERS_ALL,
          "all P refer to previous I or IDR frame (Stream with frame rate: F)",
        "three"},
    {0, NULL, NULL}
  };

  if (!layers_type) {
    layers_type = g_enum_register_static ("GstCEH264EncLayers", layers_types);
  }
  return layers_type;
}

enum
{
  GST_CE_H264ENC_SVCSYNTAX_SW = 0,
  GST_CE_H264ENC_SVCSYNTAX_SVC_SW,
  GST_CE_H264ENC_SVCSYNTAX_MMCO,
  GST_CE_H264ENC_SVCSYNTAX_SVC_MMCO,
};

#define GST_CE_H264ENC_SVCSYNTAX_TYPE (gst_cevidenc_svcsyntax_get_type())
static GType
gst_cevidenc_svcsyntax_get_type (void)
{
  static GType svcsyntax_type = 0;

  static const GEnumValue svcsyntax_types[] = {
    {GST_CE_H264ENC_SVCSYNTAX_SW,
        "SVC disabled sliding window enabled", "sw"},
    {GST_CE_H264ENC_SVCSYNTAX_SVC_SW,
        "SVC enabled sliding window enabled", "svc-sw"},
    {GST_CE_H264ENC_SVCSYNTAX_MMCO,
        "SVC disabled MMCO enabled", "mmco"},
    {GST_CE_H264ENC_SVCSYNTAX_SVC_MMCO,
        "SVC enabled MMCO enabled", "svc-mmco"},
    {0, NULL, NULL}
  };

  if (!svcsyntax_type) {
    svcsyntax_type =
        g_enum_register_static ("GstCEH264EncSeqSvcSyntax", svcsyntax_types);
  }
  return svcsyntax_type;
}

enum
{
  GST_CE_H264ENC_RCALGO_CBR = 0,
  GST_CE_H264ENC_RCALGO_VBR,
  GST_CE_H264ENC_RCALGO_FIXED_QP,
  GST_CE_H264ENC_RCALGO_CVBR,
  GST_CE_H264ENC_RCALGO_RC1,
  GST_CE_H264ENC_RCALGO_CBR1,
  GST_CE_H264ENC_RCALGO_VBR1,
};

#define GST_CE_H264ENC_RCALGO_TYPE (gst_cevidenc_rcalgo_get_type())
static GType
gst_cevidenc_rcalgo_get_type (void)
{
  static GType rcalgo_type = 0;

  static const GEnumValue rcalgo_types[] = {
    {GST_CE_H264ENC_RCALGO_CBR, "CBR", "cbr"},
    {GST_CE_H264ENC_RCALGO_VBR, "VBR", "vbr"},
    {GST_CE_H264ENC_RCALGO_FIXED_QP, "Fixed QP", "fixedqp"},
    {GST_CE_H264ENC_RCALGO_CVBR, "CVBR", "cvbr"},
    {GST_CE_H264ENC_RCALGO_RC1, "RC1", "rc1"},
    {GST_CE_H264ENC_RCALGO_CBR1, "CBR1", "cbr1"},
    {GST_CE_H264ENC_RCALGO_VBR1, "VBR1", "vbr1"},
    {0, NULL, NULL}
  };

  if (!rcalgo_type) {
    rcalgo_type = g_enum_register_static ("GstCEH264EncRCAlgo", rcalgo_types);
  }
  return rcalgo_type;
}

static void gst_ce_h264enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_ce_h264enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

#define gst_ce_h264enc_parent_class parent_class
G_DEFINE_TYPE (GstCEH264Enc, gst_ce_h264enc, GST_TYPE_CEVIDENC);

static void
gst_ce_h264enc_class_init (GstCEH264EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstCEVidEncClass *cevidenc_class;
  GstPadTemplate *srctempl = NULL, *sinktempl = NULL;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  cevidenc_class = (GstCEVidEncClass *) klass;



  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_ce_h264enc_set_property;
  gobject_class->get_property = gst_ce_h264enc_get_property;

  g_object_class_install_property (gobject_class, PROP_BYTESTREAM,
      g_param_spec_boolean ("bytestream",
          "Byte-stream",
          "Generate h264 NAL unit stream instead of 'packetized' stream (no codec_data is generated)",
          PROP_BYTESTREAM_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_HEADERS,
      g_param_spec_boolean ("headers",
          "Include on the stream the SPS/PPS headers",
          "Include on the stream the SPS/PPS headers",
          PROP_HEADERS_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SINGLE_NALU,
      g_param_spec_boolean ("single-nalu",
          "Buffers contains a single NALU",
          "Buffers contains a single NALU",
          PROP_SINGLE_NALU_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_PROFILE,
      g_param_spec_enum ("profile", "Profile",
          "Profile identification for the encoder", GST_CE_H264ENC_PROFILE_TYPE,
          PROP_PROFILE_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LEVEL,
      g_param_spec_enum ("level", "Level",
          "Level identification for the encoder", GST_CE_H264ENC_LEVEL_TYPE,
          PROP_LEVEL_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  g_object_class_install_property (gobject_class, PROP_ENTROPYMODE,
      g_param_spec_enum ("entropy", "Entropy",
          "Flag for Entropy Coding Mode", GST_CE_H264ENC_ENTROPY_TYPE,
          PROP_ENTROPYMODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_T8X8INTRA,
      g_param_spec_boolean ("t8x8intra",
          "Enable 8x8 Transform for I Frame",
          "Enable 8x8 Transform for I Frame (only for High Profile)",
          PROP_T8X8INTRA_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_T8X8INTER,
      g_param_spec_boolean ("t8x8inter",
          "Enable 8x8 Transform for P Frame",
          "Enable 8x8 Transform for P Frame (only for High Profile)",
          PROP_T8X8INTER_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SEQSCALING,
      g_param_spec_enum ("seqscaling", "Sequence Scaling",
          "Use of sequence scaling matrix", GST_CE_H264ENC_SEQSCALING_TYPE,
          PROP_SEQSCALING_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ENCQUALITY,
      g_param_spec_enum ("encquality", "Encoder quality",
          "Encoder quality setting", GST_CE_H264ENC_QUALITY_TYPE,
          PROP_ENCQUALITY_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ENABLETCM,
      g_param_spec_boolean ("enabletcm",
          "Enable ARM TCM memory usage",
          "When encquality is 0, this flag controls if TCM memory "
          "should be used (otherwise is ignored and default to yes)",
          PROP_ENABLETCM_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_DDRBUF,
      g_param_spec_boolean ("ddrbuf",
          "Use DDR buffers",
          "Use DDR buffers instead of IMCOP buffers",
          PROP_DDRBUF_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_NTEMPLAYERS,
      g_param_spec_enum ("ntemplayers", "Number of temporal Layers for SVC",
          "Number of temporal Layers for SVC", GST_CE_H264ENC_LAYERS_TYPE,
          PROP_NTEMPLAYERS_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SVCSYNTAXEN,
      g_param_spec_enum ("svcsyntaxen", "SVC Syntax Enable",
          "Control for SVC syntax and DPB management",
          GST_CE_H264ENC_SVCSYNTAX_TYPE,
          PROP_SVCSYNTAXEN_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_QPINTRA,
      g_param_spec_int ("qpintra",
          "qpintra",
          "Quantization Parameter (QP) for I frames (only valid when "
          "rate control is disabled or is fixed QP)",
          1, 51, PROP_QPINTRA_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_QPINTER,
      g_param_spec_int ("qpinter",
          "qpinter",
          "Quantization Parameter (QP) for P frame (only valid when "
          "rate control is disabled or is fixed QP)",
          1, 41, PROP_QPINTER_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_RCALGO,
      g_param_spec_enum ("rcalgo", "Rate control Algorithm",
          "Rate Control Algorithm (requires ratecontrol set to 5)",
          GST_CE_H264ENC_RCALGO_TYPE,
          PROP_RCALGO_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AIRRATE,
      g_param_spec_int ("airrate",
          "Adaptive intra refresh",
          "Adaptive intra refresh. This indicates the maximum number of MBs"
          "(per frame) that can be refreshed using AIR.",
          0, G_MAXINT32, PROP_AIRRATE_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_IDRINTERVAL,
      g_param_spec_int ("idrinterval",
          "Interval between two consecutive IDR frames",
          "Interval between two consecutive IDR frames",
          0, G_MAXINT32, PROP_IDRINTERVAL_DEFAULT, G_PARAM_READWRITE));

  /* pad templates */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ce_h264enc_sink_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ce_h264enc_src_pad_template));

  gst_element_class_set_static_metadata (element_class,
      "CE H.264 video encoder", "Codec/Encoder/Video",
      "Encode video in H.264 format",
      "Melissa Montero <melissa.montero@ridgerun.com>");

  cevidenc_class->codec_name = "h264enc";
  /*$
   * TODO
   * Set cevidenc klass virtual functions
   */
  //~ GST_DEBUG_CATEGORY_INIT (h264enc_debug, "ce_h264enc", 0,
  //~ "H.264 encoding element");
}

static void
gst_ce_h264enc_init (GstCEH264Enc * h264enc)
{
  GstCEVidEnc *cevidenc = (GstCEVidEnc *) (h264enc);
  IH264VENC_Params *h264_params = NULL;
  IH264VENC_DynamicParams *h264_dyn_params = NULL;

  GST_DEBUG ("setup H.264 parameters");
  /* Alloc the params and set a default value */
  h264_params = g_malloc0 (sizeof (IH264VENC_Params));
  if (!h264_params)
    goto fail_alloc;
  *h264_params = IH264VENC_PARAMS;

  h264_dyn_params = g_malloc0 (sizeof (IH264VENC_DynamicParams));
  if (!h264_dyn_params)
    goto fail_alloc;
  *h264_dyn_params = H264VENC_TI_IH264VENC_DYNAMICPARAMS;

  if (cevidenc->codec_params) {
    GST_DEBUG ("codec params not NULL, copy and free them");
    h264_params->videncParams = *cevidenc->codec_params;
    g_free (cevidenc->codec_params);
  }
  cevidenc->codec_params = (VIDENC1_Params *) h264_params;

  if (cevidenc->codec_dyn_params) {
    GST_DEBUG ("codec dynamic params not NULL, copy and free them");
    h264_dyn_params->videncDynamicParams = *cevidenc->codec_dyn_params;
    g_free (cevidenc->codec_dyn_params);
  }
  cevidenc->codec_dyn_params = (VIDENC1_DynamicParams *) h264_dyn_params;

  /* Add the extends params to the original params */
  cevidenc->codec_params->size = sizeof (IH264VENC_Params);
  cevidenc->codec_dyn_params->size = sizeof (IH264VENC_DynamicParams);

  return;

fail_alloc:
  {
    GST_WARNING_OBJECT (cevidenc, "failed to allocate H.264 params");
    if (h264_params)
      g_free (h264_params);
    if (h264_dyn_params)
      g_free (h264_params);
    return;
  }
}

static gboolean
gst_ce_h264enc_fetch_header (guint8 * data, gint buffer_size,
    nalUnit * sps, nalUnit * pps)
{
  gint i;
  gint nal_type;
  gint found = 0;
  nalUnit *nalu = NULL;
  gint32 state;

  const gint32 start_code = 0x00000001;

  GST_DEBUG ("fetching header PPS and SPS");
  GST_MEMDUMP ("Header", data, buffer_size);
  /*Initialize to a pattern that does not match the start code */
  state = ~(start_code);
  for (i = 0; i < (buffer_size - NAL_LENGTH); i++) {
    state = ((state << 8) | data[i]);

    /* In bytestream format each NAL si preceded by 
     * a four byte start code: 0x00 0x00 0x00 0x01.
     * The byte after this code indicates the NAL type,
     * we're looking for the SPS(0x07) and PPS(0x08) NAL*/
    if (state == start_code) {
      if (nalu) {
        nalu->size = i - nalu->index - NAL_LENGTH + 1;
        nalu = NULL;
      }

      if (found == 2)
        break;

      nal_type = (data[i + 1]) & 0x1f;
      if (nal_type == GST_H264_NAL_SPS)
        nalu = sps;
      else if (nal_type == GST_H264_NAL_PPS)
        nalu = pps;
      else
        continue;

      nalu->type = nal_type;
      nalu->index = i + 1;

      i++;
      found++;
    }
  }

  if (i >= (buffer_size - 5))
    nalu->size = buffer_size - nalu->index;

  return TRUE;
}

static gboolean
gst_ce_h264enc_get_header (GstCEVidEnc * cevidenc, GstBuffer ** buf)
{
  h264PrivateData *h264enc = (h264PrivateData *) cevidenc->codec_private;
  VIDENC1_Status enc_status;
  VIDENC1_InArgs in_args;
  VIDENC1_OutArgs out_args;
  GstBuffer *header_buf;
  GstMapInfo info;
  gint ret;

  GST_DEBUG_OBJECT (cevidenc, "get H.264 header");
  if ((!cevidenc->codec_handle) || (!cevidenc->codec_dyn_params))
    return FALSE;

  enc_status.size = sizeof (VIDENC1_Status);
  enc_status.data.buf = NULL;

  cevidenc->codec_dyn_params->generateHeader = XDM_GENERATE_HEADER;
  ret = VIDENC1_control (cevidenc->codec_handle, XDM_SETPARAMS,
      cevidenc->codec_dyn_params, &enc_status);
  if (ret != VIDENC1_EOK)
    goto fail_control_params;

  /*Allocate an output buffer for the header */
  header_buf = gst_buffer_new_allocate (cevidenc->allocator, 200,
      &cevidenc->alloc_params);
  if (!gst_buffer_map (header_buf, &info, GST_MAP_WRITE))
    return FALSE;

  cevidenc->outbuf_desc.bufs = (XDAS_Int8 **) & (info.data);

  /* Set output and input arguments for the encode process */
  in_args.size = sizeof (IVIDENC1_InArgs);
  in_args.inputID = 1;
  in_args.topFieldFirstFlag = 1;

  out_args.size = sizeof (VIDENC1_OutArgs);

  /* Generate the header */
  ret =
      VIDENC1_process (cevidenc->codec_handle, &cevidenc->inbuf_desc,
      &cevidenc->outbuf_desc, &in_args, &out_args);
  if (ret != VIDENC1_EOK)
    goto fail_encode;

  gst_buffer_unmap (header_buf, &info);

  cevidenc->codec_dyn_params->generateHeader = XDM_ENCODE_AU;
  ret = VIDENC1_control (cevidenc->codec_handle, XDM_SETPARAMS,
      cevidenc->codec_dyn_params, &enc_status);
  if (ret != VIDENC1_EOK)
    goto fail_control_params;

  h264enc->header_size = out_args.bytesGenerated;
  *buf = header_buf;

  return TRUE;

fail_control_params:
  {
    GST_WARNING_OBJECT (cevidenc, "Failed to set dynamic parameters, "
        "status error %x, %d", (unsigned int) enc_status.extendedError, ret);
    return FALSE;
  }
fail_encode:
  {
    GST_WARNING_OBJECT (cevidenc,
        "Failed header encode process with extended error: 0x%x",
        (unsigned int) out_args.extendedError);
    return FALSE;
  }

}

static gboolean
gst_ce_h264enc_get_codec_data (GstCEVidEnc * cevidenc, GstBuffer ** codec_data)
{
  h264PrivateData *h264enc = (h264PrivateData *) cevidenc->codec_private;

  GstBuffer *buf;
  GstMapInfo info;
  nalUnit sps, pps;
  guint8 *header, *buffer, *sps_ptr;
  gint codec_data_size;
  gint num_sps = 1;
  gint num_pps = 1;
  gint nal_idx;

  GST_DEBUG_OBJECT (cevidenc, "generating codec data..");

  /*Get the bytestream header from codec */
  if (!gst_ce_h264enc_get_header (cevidenc, &buf))
    return FALSE;

  /*Get pointer to the header data */
  if (!gst_buffer_map (buf, &info, GST_MAP_READ))
    return FALSE;

  header = info.data;
  gst_buffer_unmap (buf, &info);

  /*Parse the PPS and SPS */
  gst_ce_h264enc_fetch_header (header, h264enc->header_size, &sps, &pps);

  if (sps.type != 7 || pps.type != 8 || sps.size < 4 || pps.size < 1) {
    GST_WARNING_OBJECT (cevidenc, "unexpected H.264 header");
    return FALSE;
  }

  GST_MEMDUMP ("SPS", &header[sps.index], sps.size);
  GST_MEMDUMP ("PPS", &header[pps.index], pps.size);

  /*
   *      -: avc codec data:-
   *  -----------------------------------
   *  1 byte  - version
   *  1 byte  - h.264 stream profile
   *  1 byte  - h.264 compatible profiles
   *  1 byte  - h.264 stream level
   *  6 bits  - reserved set to 63
   *  2 bits  - NAL length
   *            ( 0 - 1 byte; 1 - 2 bytes; 3 - 4 bytes)
   *  1 byte  - number of SPS
   *  2 bytes - SPS length
   *  for (i=0; i < number of SPS; i++) {
   *      SPS length bytes - SPS NAL unit
   *  }
   *  1 byte  - number of PPS
   *  2 bytes - PPS length
   *  for (i=0; i < number of PPS; i++) {
   *      PPS length bytes - PPS NAL unit
   *  }
   * ------------------------------------------
   */
  codec_data_size = sps.size + pps.size + 11;
  buffer = g_malloc (codec_data_size);
  /* SPS pointer, skip NAL unit type */
  sps_ptr = &header[sps.index] + 1;

  buffer[0] = 1;
  buffer[1] = sps_ptr[0];
  buffer[2] = sps_ptr[1];
  buffer[3] = sps_ptr[2];
  buffer[4] = 0xfc | (4 - 1);   /*0xfc for the 6 bits reserved */
  buffer[5] = 0xe0 | num_sps;   /*0xe0 for the 3 bits reserved */

  nal_idx = 6;
  GST_WRITE_UINT16_BE (buffer + nal_idx, sps.size);
  nal_idx += 2;
  memcpy (buffer + nal_idx, &header[sps.index], sps.size);
  nal_idx += sps.size;

  buffer[nal_idx++] = num_pps;  /* number of PPSs */
  GST_WRITE_UINT16_BE (buffer + nal_idx, pps.size);
  nal_idx += 2;
  memcpy (buffer + nal_idx, &header[pps.index], pps.size);
  nal_idx += pps.size;

  GST_MEMDUMP ("Codec data", buffer, codec_data_size);
  gst_buffer_unref (buf);

  *codec_data = gst_buffer_new_wrapped (buffer, codec_data_size);

  return TRUE;
}

static gboolean
gst_ce_h264enc_set_src_caps (GObject * object, GstCaps ** caps,
    GstBuffer ** codec_data)
{
  GstCEVidEnc *cevidenc = (GstCEVidEnc *) (object);
  h264PrivateData *h264enc;
  GstStructure *s;
  const gchar *stream_format;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (cevidenc, "setting H.264 caps");
  if (!cevidenc->codec_private)
    goto fail_no_private_data;

  h264enc = cevidenc->codec_private;
  *caps = gst_caps_make_writable (*caps);
  s = gst_caps_get_structure (*caps, 0);

  stream_format = gst_structure_get_string (s, "stream-format");
  h264enc->current_stream_format = GST_CE_H264ENC_STREAM_FORMAT_FROM_PROPERTY;
  if (stream_format) {
    if (!strcmp (stream_format, "avc")) {
      GST_DEBUG_OBJECT (cevidenc, "stream format: avc");
      h264enc->current_stream_format = GST_CE_H264ENC_STREAM_FORMAT_AVC;
    } else if (!strcmp (stream_format, "byte-stream")) {
      GST_DEBUG_OBJECT (cevidenc, "stream format: byte-stream");
      h264enc->current_stream_format = GST_CE_H264ENC_STREAM_FORMAT_BYTE_STREAM;
    }
  }

  if (h264enc->current_stream_format ==
      GST_CE_H264ENC_STREAM_FORMAT_FROM_PROPERTY) {
    /* means we have both in caps and from property should be the option */
    GST_DEBUG_OBJECT (cevidenc, "setting stream format from property");
    if (h264enc->byte_stream) {
      GST_DEBUG_OBJECT (cevidenc, "stream format: byte-stream");
      h264enc->current_stream_format = GST_CE_H264ENC_STREAM_FORMAT_BYTE_STREAM;
    } else {
      GST_DEBUG_OBJECT (cevidenc, "stream format: avc");
      h264enc->current_stream_format = GST_CE_H264ENC_STREAM_FORMAT_AVC;
    }
  }

  if (h264enc->current_stream_format == GST_CE_H264ENC_STREAM_FORMAT_AVC) {
    ret = gst_ce_h264enc_get_codec_data (cevidenc, codec_data);
    gst_structure_set (s, "stream-format", G_TYPE_STRING, "avc", NULL);
  } else {
    gst_structure_set (s, "stream-format", G_TYPE_STRING, "byte-stream", NULL);
  }
  GST_DEBUG_OBJECT (cevidenc, "H.264 caps %s", gst_caps_to_string (*caps));

  return ret;

fail_no_private_data:
  {
    GST_WARNING_OBJECT (cevidenc, "no codec private data available");
    return FALSE;
  }

}

static void
gst_ce_h264enc_setup (GObject * object)
{
  GstCEVidEnc *cevidenc = (GstCEVidEnc *) (object);
  h264PrivateData *h264enc;
  IH264VENC_Params *h264_params = NULL;
  IH264VENC_DynamicParams *h264_dyn_params = NULL;

  GST_DEBUG ("setup H.264 parameters");
  /* Alloc the params and set a default value */
  h264_params = g_malloc0 (sizeof (IH264VENC_Params));
  if (!h264_params)
    goto fail_alloc;
  *h264_params = IH264VENC_PARAMS;

  h264_dyn_params = g_malloc0 (sizeof (IH264VENC_DynamicParams));
  if (!h264_dyn_params)
    goto fail_alloc;
  *h264_dyn_params = H264VENC_TI_IH264VENC_DYNAMICPARAMS;

  if (cevidenc->codec_params) {
    GST_DEBUG ("codec params not NULL, copy and free them");
    h264_params->videncParams = *cevidenc->codec_params;
    g_free (cevidenc->codec_params);
  }
  cevidenc->codec_params = (VIDENC1_Params *) h264_params;

  if (cevidenc->codec_dyn_params) {
    GST_DEBUG ("codec dynamic params not NULL, copy and free them");
    h264_dyn_params->videncDynamicParams = *cevidenc->codec_dyn_params;
    g_free (cevidenc->codec_dyn_params);
  }
  cevidenc->codec_dyn_params = (VIDENC1_DynamicParams *) h264_dyn_params;

  /* Add the extends params to the original params */
  cevidenc->codec_params->size = sizeof (IH264VENC_Params);
  cevidenc->codec_dyn_params->size = sizeof (IH264VENC_DynamicParams);
  GST_DEBUG_OBJECT (cevidenc, "allocating H.264 private data");
  if (cevidenc->codec_private)
    g_free (cevidenc->codec_private);

  cevidenc->codec_private = g_malloc0 (sizeof (h264PrivateData));
  if (!cevidenc->codec_private) {
    GST_WARNING_OBJECT (cevidenc, "Failed to allocate codec private data");
    return;
  }
  h264enc = (h264PrivateData *) cevidenc->codec_private;

  /* Setting properties defaults */
  h264enc->byte_stream = PROP_BYTESTREAM_DEFAULT;
  h264enc->single_nalu = PROP_SINGLE_NALU_DEFAULT;
  h264enc->headers = PROP_HEADERS_DEFAULT;

  h264_params->profileIdc = PROP_PROFILE_DEFAULT;
  h264_params->levelIdc = PROP_LEVEL_DEFAULT;
  h264_params->entropyMode = PROP_ENTROPYMODE_DEFAULT;
  h264_params->transform8x8FlagIntraFrame = PROP_T8X8INTRA_DEFAULT;
  h264_params->transform8x8FlagInterFrame = PROP_T8X8INTER_DEFAULT;
  h264_params->seqScalingFlag = PROP_SEQSCALING_DEFAULT;
  h264_params->encQuality = PROP_ENCQUALITY_DEFAULT;
  h264_params->enableARM926Tcm = PROP_ENABLETCM_DEFAULT;
  h264_params->enableDDRbuff = PROP_DDRBUF_DEFAULT;
  h264_params->numTemporalLayers = PROP_NTEMPLAYERS_DEFAULT;
  h264_params->svcSyntaxEnable = PROP_SVCSYNTAXEN_DEFAULT;

  h264_dyn_params->airRate = PROP_AIRRATE_DEFAULT;
  h264_dyn_params->intraFrameQP = PROP_QPINTRA_DEFAULT;
  h264_dyn_params->interPFrameQP = PROP_QPINTER_DEFAULT;
  h264_dyn_params->rcAlgo = PROP_RCALGO_DEFAULT;
  h264_dyn_params->idrFrameInterval = PROP_IDRINTERVAL_DEFAULT;

  return;

fail_alloc:
  {
    GST_WARNING_OBJECT (cevidenc, "failed to allocate H.264 params");
    if (h264_params)
      g_free (h264_params);
    if (h264_dyn_params)
      g_free (h264_params);
    return;
  }
}

static gboolean
gst_ce_h264enc_post_process (GObject * object, GstBuffer * buffer)
{
  GstCEVidEnc *cevidenc = (GstCEVidEnc *) (object);
  h264PrivateData *h264enc = (h264PrivateData *) cevidenc->codec_private;
  GstMapInfo info;
  guint8 *data;
  gint i, mark = 0;
  gint curr_nal_type = -1;
  gint prev_nal_type = -1;
  gint size;
  gint32 state;

  const gint32 start_code = 0x00000001;

  if (!h264enc) {
    GST_ERROR_OBJECT (cevidenc, "no H.264 private data, run setup first");
    return FALSE;
  }

  if (h264enc->current_stream_format ==
      GST_CE_H264ENC_STREAM_FORMAT_BYTE_STREAM)
    return TRUE;

  GST_DEBUG_OBJECT (cevidenc, "parsing byte-stream to avc");

  if (!gst_buffer_map (buffer, &info, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (cevidenc, "failed to map buffer");
    return FALSE;
  }

  data = info.data;
  size = info.size;
  /*Initialize to a pattern that does not match the start code */
  state = ~(start_code);
  for (i = 0; i < size - NAL_LENGTH; i++) {
    state = ((state << 8) | data[i]);
    if (state == start_code) {
      prev_nal_type = curr_nal_type;
      curr_nal_type = (data[i + 1]) & 0x1f;
      GST_DEBUG_OBJECT (cevidenc, "NAL unit %d", curr_nal_type);
      if (h264enc->single_nalu) {
        if ((curr_nal_type == GST_H264_NAL_SPS)
            || (curr_nal_type == GST_H264_NAL_PPS)) {
          GST_DEBUG_OBJECT (cevidenc, "single NALU, found a I-frame");
          /* Caution: here we are asumming the output buffer only 
           * has one memory block*/
          info.memory->offset = h264enc->header_size;
          gst_buffer_set_size (buffer, size - h264enc->header_size);
          mark = i + h264enc->header_size + 1;
        } else {
          GST_DEBUG_OBJECT (cevidenc, "single NALU, found a P-frame");
          mark = i + 1;
        }
        i = size - NAL_LENGTH;
        break;
      } else {
        if ((prev_nal_type == GST_H264_NAL_SPS
                || prev_nal_type == GST_H264_NAL_PPS)
            && !h264enc->headers) {
          /* Discard anything previous to the SPS and PPS */
          /* Caution: here we are asumming the output buffer  
           * has only one memory block*/
          info.memory->offset = i - NAL_LENGTH + 1;
          gst_buffer_set_size (buffer, size - (i - NAL_LENGTH + 1));
          GST_DEBUG_OBJECT (cevidenc, "SPS and PPS discard");
        } else if (prev_nal_type != -1) {
          /* Replace the NAL start code with the length */
          gint length = i - mark - NAL_LENGTH + 1;
          gint k;
          for (k = 1; k <= 4; k++) {
            data[mark - k] = length & 0xff;
            length >>= 8;
          }
        }
      }
      /* Mark where next NALU starts */
      mark = i + 1;
    }
  }

  if (i == (size - 4)) {
    /* We reach the end of the buffer */
    if (curr_nal_type != -1) {
      gint k;
      gint length = size - mark;
      GST_DEBUG_OBJECT (cevidenc, "Replace the NAL start code "
          "with the length %d buffer %d", length, size);
      for (k = 1; k <= 4; k++) {
        data[mark - k] = length & 0xff;
        length >>= 8;
      }
    }
  }

  gst_buffer_unmap (buffer, &info);

  return TRUE;
}

static void
gst_ce_h264enc_install_properties (GObjectClass * gobject_class, guint base)
{

  g_object_class_install_property (gobject_class, base + PROP_BYTESTREAM,
      g_param_spec_boolean ("bytestream",
          "Byte-stream",
          "Generate h264 NAL unit stream instead of 'packetized' stream (no codec_data is generated)",
          PROP_BYTESTREAM_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, base + PROP_HEADERS,
      g_param_spec_boolean ("headers",
          "Include on the stream the SPS/PPS headers",
          "Include on the stream the SPS/PPS headers",
          PROP_HEADERS_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, base + PROP_SINGLE_NALU,
      g_param_spec_boolean ("single-nalu",
          "Buffers contains a single NALU",
          "Buffers contains a single NALU",
          PROP_SINGLE_NALU_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, base + PROP_PROFILE,
      g_param_spec_enum ("profile", "Profile",
          "Profile identification for the encoder", GST_CE_H264ENC_PROFILE_TYPE,
          PROP_PROFILE_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, base + PROP_LEVEL,
      g_param_spec_enum ("level", "Profile",
          "Level identification for the encoder", GST_CE_H264ENC_LEVEL_TYPE,
          PROP_LEVEL_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  g_object_class_install_property (gobject_class, base + PROP_ENTROPYMODE,
      g_param_spec_enum ("entropy", "Entropy",
          "Flag for Entropy Coding Mode", GST_CE_H264ENC_ENTROPY_TYPE,
          PROP_ENTROPYMODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, base + PROP_T8X8INTRA,
      g_param_spec_boolean ("t8x8intra",
          "Enable 8x8 Transform for I Frame",
          "Enable 8x8 Transform for I Frame (only for High Profile)",
          PROP_T8X8INTRA_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, base + PROP_T8X8INTER,
      g_param_spec_boolean ("t8x8inter",
          "Enable 8x8 Transform for P Frame",
          "Enable 8x8 Transform for P Frame (only for High Profile)",
          PROP_T8X8INTER_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, base + PROP_SEQSCALING,
      g_param_spec_enum ("seqscaling", "Sequence Scaling",
          "Use of sequence scaling matrix", GST_CE_H264ENC_SEQSCALING_TYPE,
          PROP_SEQSCALING_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, base + PROP_ENCQUALITY,
      g_param_spec_enum ("encquality", "Encoder quality",
          "Encoder quality setting", GST_CE_H264ENC_QUALITY_TYPE,
          PROP_ENCQUALITY_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, base + PROP_ENABLETCM,
      g_param_spec_boolean ("enabletcm",
          "Enable ARM TCM memory usage",
          "When encquality is 0, this flag controls if TCM memory "
          "should be used (otherwise is ignored and default to yes)",
          PROP_ENABLETCM_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, base + PROP_DDRBUF,
      g_param_spec_boolean ("ddrbuf",
          "Use DDR buffers",
          "Use DDR buffers instead of IMCOP buffers",
          PROP_DDRBUF_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, base + PROP_NTEMPLAYERS,
      g_param_spec_enum ("ntemplayers", "Number of temporal Layers for SVC",
          "Number of temporal Layers for SVC", GST_CE_H264ENC_LAYERS_TYPE,
          PROP_NTEMPLAYERS_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, base + PROP_SVCSYNTAXEN,
      g_param_spec_enum ("svcsyntaxen", "SVC Syntax Enable",
          "Control for SVC syntax and DPB management",
          GST_CE_H264ENC_SVCSYNTAX_TYPE,
          PROP_SVCSYNTAXEN_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, base + PROP_QPINTRA,
      g_param_spec_int ("qpintra",
          "qpintra",
          "Quantization Parameter (QP) for I frames (only valid when "
          "rate control is disabled or is fixed QP)",
          1, 51, PROP_QPINTRA_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, base + PROP_QPINTER,
      g_param_spec_int ("qpinter",
          "qpinter",
          "Quantization Parameter (QP) for P frame (only valid when "
          "rate control is disabled or is fixed QP)",
          1, 41, PROP_QPINTER_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, base + PROP_RCALGO,
      g_param_spec_enum ("rcalgo", "Rate control Algorithm",
          "Rate Control Algorithm (requires ratecontrol set to 5)",
          GST_CE_H264ENC_RCALGO_TYPE,
          PROP_RCALGO_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, base + PROP_AIRRATE,
      g_param_spec_int ("airrate",
          "Adaptive intra refresh",
          "Adaptive intra refresh. This indicates the maximum number of MBs"
          "(per frame) that can be refreshed using AIR.",
          0, G_MAXINT32, PROP_AIRRATE_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, base + PROP_IDRINTERVAL,
      g_param_spec_int ("idrinterval",
          "Interval between two consecutive IDR frames",
          "Interval between two consecutive IDR frames",
          0, G_MAXINT32, PROP_IDRINTERVAL_DEFAULT, G_PARAM_READWRITE));
}

static void
gst_ce_h264enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCEVidEnc *cevidenc = (GstCEVidEnc *) (object);
  GstCEH264Enc *h264enc = (GstCEH264Enc *) (object);
  IH264VENC_Params *params;
  IH264VENC_DynamicParams *dyn_params;
  VIDENC1_Status enc_status;
  gboolean set_params = FALSE;
  guint ret;

  params = (IH264VENC_Params *) cevidenc->codec_params;
  dyn_params = (IH264VENC_DynamicParams *) cevidenc->codec_dyn_params;

  if ((!params) || (!dyn_params)) {
    GST_WARNING_OBJECT (cevidenc, "couldn't set property");
    return;
  }

  switch (prop_id) {
    case PROP_BYTESTREAM:
      h264enc->byte_stream = g_value_get_boolean (value);
      break;
    case PROP_HEADERS:
      h264enc->headers = g_value_get_boolean (value);
      break;
    case PROP_SINGLE_NALU:
      h264enc->single_nalu = g_value_get_boolean (value);
      break;
    case PROP_PROFILE:
      if (!cevidenc->codec_handle)
        params->profileIdc = g_value_get_enum (value);
      else
        goto fail_static_prop;
      break;
    case PROP_LEVEL:
      if (!cevidenc->codec_handle)
        params->levelIdc = g_value_get_enum (value);
      else
        goto fail_static_prop;
      break;
    case PROP_ENTROPYMODE:
      if (!cevidenc->codec_handle)
        params->entropyMode = g_value_get_enum (value);
      else
        goto fail_static_prop;
      break;
    case PROP_T8X8INTRA:
      if (!cevidenc->codec_handle)
        params->transform8x8FlagIntraFrame =
            g_value_get_boolean (value) ? 1 : 0;
      else
        goto fail_static_prop;
      break;
    case PROP_T8X8INTER:
      if (!cevidenc->codec_handle)
        params->transform8x8FlagInterFrame =
            g_value_get_boolean (value) ? 1 : 0;
      else
        goto fail_static_prop;
      break;
    case PROP_ENCQUALITY:
      if (!cevidenc->codec_handle)
        params->encQuality = g_value_get_enum (value);
      else
        goto fail_static_prop;
      break;
    case PROP_ENABLETCM:
      if (!cevidenc->codec_handle)
        params->enableARM926Tcm = g_value_get_boolean (value) ? 1 : 0;
      else
        goto fail_static_prop;
      break;
    case PROP_DDRBUF:
      if (!cevidenc->codec_handle)
        params->enableDDRbuff = g_value_get_boolean (value) ? 1 : 0;
      else
        goto fail_static_prop;
      break;
    case PROP_NTEMPLAYERS:
      if (!cevidenc->codec_handle)
        params->numTemporalLayers = g_value_get_enum (value);
      else
        goto fail_static_prop;
      break;
    case PROP_SVCSYNTAXEN:
      if (!cevidenc->codec_handle)
        params->svcSyntaxEnable = g_value_get_enum (value);
      else
        goto fail_static_prop;
      break;
    case PROP_SEQSCALING:
      if (!cevidenc->codec_handle)
        params->seqScalingFlag = g_value_get_enum (value);
      else
        goto fail_static_prop;
      break;
    case PROP_QPINTRA:
      dyn_params->intraFrameQP = g_value_get_int (value);
      set_params = TRUE;
      break;
    case PROP_QPINTER:
      dyn_params->interPFrameQP = g_value_get_int (value);
      set_params = TRUE;
      break;
    case PROP_RCALGO:
      dyn_params->rcAlgo = g_value_get_enum (value);
      set_params = TRUE;
      break;
    case PROP_AIRRATE:
      dyn_params->airRate = g_value_get_int (value);
      set_params = TRUE;
      break;
    case PROP_IDRINTERVAL:
      dyn_params->idrFrameInterval = g_value_get_int (value);
      set_params = TRUE;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  /* Set dynamic parameters if needed */
  if (set_params && cevidenc->codec_handle) {
    enc_status.size = sizeof (VIDENC1_Status);
    enc_status.data.buf = NULL;
    ret = VIDENC1_control (cevidenc->codec_handle, XDM_SETPARAMS,
        (VIDENC1_DynamicParams *) dyn_params, &enc_status);
    if (ret != VIDENC1_EOK)
      GST_WARNING_OBJECT (cevidenc, "failed to set dynamic parameters, "
          "status error %x, %d", (guint) enc_status.extendedError, ret);
  }

  return;

fail_static_prop:
  GST_WARNING_OBJECT (cevidenc, "can't set static property when "
      "the codec is already configured");
}

static void
gst_ce_h264enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCEVidEnc *cevidenc = (GstCEVidEnc *) (object);
  GstCEH264Enc *h264enc = (GstCEH264Enc *) (object);
  IH264VENC_Params *params;
  IH264VENC_DynamicParams *dyn_params;

  params = (IH264VENC_Params *) cevidenc->codec_params;
  dyn_params = (IH264VENC_DynamicParams *) cevidenc->codec_dyn_params;

  if ((!params) || (!dyn_params)) {
    GST_WARNING_OBJECT (cevidenc, "couldn't set property");
    return;
  }

  switch (prop_id) {
    case PROP_BYTESTREAM:
      g_value_set_boolean (value, h264enc->byte_stream);
      break;
    case PROP_HEADERS:
      g_value_set_boolean (value, h264enc->headers);
      break;
    case PROP_SINGLE_NALU:
      g_value_set_boolean (value, h264enc->single_nalu);
      break;
    case PROP_PROFILE:
      g_value_set_enum (value, params->profileIdc);
      break;
    case PROP_LEVEL:
      g_value_set_enum (value, params->levelIdc);
      break;
    case PROP_ENTROPYMODE:
      g_value_set_enum (value, params->entropyMode);
      break;
    case PROP_T8X8INTRA:
      g_value_set_boolean (value,
          params->transform8x8FlagIntraFrame ? TRUE : FALSE);
      break;
    case PROP_T8X8INTER:
      g_value_set_boolean (value,
          params->transform8x8FlagInterFrame ? TRUE : FALSE);
      break;
    case PROP_ENCQUALITY:
      g_value_set_enum (value, params->encQuality);
      break;
    case PROP_ENABLETCM:
      g_value_set_boolean (value, params->enableARM926Tcm ? TRUE : FALSE);
      break;
    case PROP_DDRBUF:
      g_value_set_boolean (value, params->enableDDRbuff ? TRUE : FALSE);
      break;
    case PROP_NTEMPLAYERS:
      g_value_set_enum (value, params->numTemporalLayers);
      break;
    case PROP_SVCSYNTAXEN:
      g_value_set_enum (value, params->svcSyntaxEnable);
      break;
    case PROP_SEQSCALING:
      g_value_set_enum (value, params->seqScalingFlag);
      break;
    case PROP_QPINTRA:
      g_value_set_int (value, dyn_params->intraFrameQP);
      break;
    case PROP_QPINTER:
      g_value_set_int (value, dyn_params->interPFrameQP);
      break;
    case PROP_RCALGO:
      g_value_set_enum (value, dyn_params->rcAlgo);
      break;
    case PROP_AIRRATE:
      g_value_set_int (value, dyn_params->airRate);
      break;
    case PROP_IDRINTERVAL:
      g_value_set_int (value, dyn_params->idrFrameInterval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

GstCECodecData gst_ce_h264enc = {
  .name = "h264enc",
  .long_name = "H.264",
  .src_caps = &gst_ce_h264enc_src_caps,
  .sink_caps = &gst_ce_h264enc_sink_caps,
  .setup = gst_ce_h264enc_setup,
  .set_src_caps = gst_ce_h264enc_set_src_caps,
  .post_process = gst_ce_h264enc_post_process,
  .install_properties = gst_ce_h264enc_install_properties,
  //~ .set_property = gst_ce_h264enc_set_property,
  //~ .get_property = gst_ce_h264enc_get_property,
};
