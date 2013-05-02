/*
 * gstceaacenc.c
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
#include <ti/sdo/ce/audio1/audenc1.h>
#include <ittiam/codecs/aaclc_enc/ieaacplusenc.h>

#include "gstceaacenc.h"

#define SAMPLE_RATES " 8000, " \
                    "11025, " \
                    "12000, " \
                    "16000, " \
                    "22050, " \
                    "24000, " \
                    "32000, " \
                    "44100, " \
                    "48000, " \
                    "88200, " \
                    "96000"

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_ce_aacenc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
    "   format = (string) " GST_AUDIO_NE (S16) ", "
    "   layout = (string) interleaved, "
    "   channels=(int)[ 1, 2 ], "
    "   rate =(int){" SAMPLE_RATES "} " )
    );
/* *INDENT-ON* */

static GstStaticPadTemplate gst_ce_aacenc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "   mpegversion=(int) { 2, 4 }, "
        "   channels= (int)[ 1, 2 ], "
        "   rate = (int){" SAMPLE_RATES "} ,"
        "   stream-format=(string){ raw, adts, adif} , "
        "   base-profile = (string) lc ")
    );

enum
{
  PROP_0,
  PROP_DOWNMIX,
  PROP_STEREO_PREPROCESSING,
  PROP_INV_QUANT,
  PROP_TNS,
  PROP_FULL_BANDWIDTH,
};

static void gst_ce_aacenc_reset (GstCEAudEnc * ceaudenc);
static gboolean gst_ce_aacenc_set_src_caps (GstCEAudEnc * ceaudenc,
    GstAudioInfo * info, GstCaps ** caps, GstBuffer ** codec_data);
static gboolean gst_ce_aacenc_post_process (GstCEAudEnc * ceaudenc,
    GstBuffer * buffer);

static void gst_ce_aacenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_ce_aacenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

#define gst_ce_aacenc_parent_class parent_class
G_DEFINE_TYPE (GstCEAACEnc, gst_ce_aacenc, GST_TYPE_CEAUDENC);

static void
gst_ce_aacenc_class_init (GstCEAACEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstCEAudEncClass *ceaudenc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  ceaudenc_class = GST_CEAUDENC_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_ce_aacenc_set_property;
  gobject_class->get_property = gst_ce_aacenc_get_property;

  g_object_class_install_property (gobject_class, PROP_DOWNMIX,
      g_param_spec_boolean ("downmix",
          "Downmix", "Option to enable downmix", FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_STEREO_PREPROCESSING,
      g_param_spec_boolean ("stereo-preprocessing",
          "Use stereo preprocessing",
          "Use stereo preprocessing flag: Only applicable "
          "when sampleRate <24000 Hz and bitRate < 60000 bps.",
          FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_INV_QUANT,
      g_param_spec_int ("inv-quant",
          "Inverse quantization level",
          "Inverse quantization level", 0, 2, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_TNS,
      g_param_spec_boolean ("tns",
          "TNS enable", "Flag for TNS enable", TRUE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_FULL_BANDWIDTH,
      g_param_spec_boolean ("full-bandwidth",
          "Enable full bandwidth",
          "Flag to enable full bandwidth", FALSE, G_PARAM_READWRITE));


  /* pad templates */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ce_aacenc_sink_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ce_aacenc_src_pad_template));

  gst_element_class_set_static_metadata (element_class,
      "CE AAC-LC audio encoder", "Codec/Encoder/Audio",
      "Encode audio in AAC-LC format",
      "Melissa Montero <melissa.montero@ridgerun.com>");

  ceaudenc_class->codec_name = "aaclcenc";
  ceaudenc_class->samples = 1024;
  ceaudenc_class->reset = gst_ce_aacenc_reset;
  ceaudenc_class->set_src_caps = gst_ce_aacenc_set_src_caps;
  /*$
   * TODO
   * Do we want to set aac specific debug?
   */
  //~ GST_DEBUG_CATEGORY_INIT (aacenc_debug, "ce_aacenc", 0,
  //~ "AAC-LC encoding element");
}

static void
gst_ce_aacenc_init (GstCEAACEnc * aacenc)
{
  GstCEAudEnc *ceaudenc = GST_CEAUDENC (aacenc);
  ITTIAM_EAACPLUSENC_Params *aac_params = NULL;

  GST_DEBUG_OBJECT (aacenc, "setup AAC parameters");
  /* Alloc the params and set a default value */
  aac_params = g_malloc0 (sizeof (ITTIAM_EAACPLUSENC_Params));
  if (!aac_params)
    goto fail_alloc;
  *aac_params = EAACPLUSENCODER_ITTIAM_PARAMS;

  if (ceaudenc->codec_params) {
    GST_DEBUG_OBJECT (aacenc, "codec params not NULL, copy and free them");
    aac_params->s_iaudenc_params = *ceaudenc->codec_params;
    g_free (ceaudenc->codec_params);
  }
  ceaudenc->codec_params = (AUDENC1_Params *) aac_params;

  /* Add the extends params to the original params */
  ceaudenc->codec_params->size = sizeof (ITTIAM_EAACPLUSENC_Params);

  gst_ce_aacenc_reset (ceaudenc);

  return;
fail_alloc:
  {
    GST_WARNING_OBJECT (ceaudenc, "failed to allocate H.AAC params");
    if (aac_params)
      g_free (aac_params);
    return;
  }
}

static gboolean
gst_ce_aacenc_set_src_caps (GstCEAudEnc * ceaudenc, GstAudioInfo * info,
    GstCaps ** caps, GstBuffer ** codec_data)
{
  GstCEAACEnc *aacenc = GST_CE_AACENC (ceaudenc);
  const gchar *stream_format = NULL;
  gboolean ret = TRUE;

  ITTIAM_EAACPLUSENC_Params *params;
  params = (ITTIAM_EAACPLUSENC_Params *) ceaudenc->codec_params;

  GST_DEBUG_OBJECT (aacenc, "setting AAC caps");
  if (*caps && gst_caps_get_size (*caps) > 0) {
    GstStructure *s = gst_caps_get_structure (*caps, 0);

    if ((stream_format = gst_structure_get_string (s, "stream-format"))) {
      if (strcmp (stream_format, "adts") == 0) {
        GST_DEBUG_OBJECT (aacenc, "use ADTS format for output");
        params->use_ADTS = 1;
        params->use_ADIF = 0;
      } else if (strcmp (stream_format, "adif") == 0) {
        GST_DEBUG_OBJECT (aacenc, "use ADIF format for output");
        params->use_ADTS = 0;
        params->use_ADIF = 1;
      } else if (strcmp (stream_format, "raw") == 0) {
        GST_DEBUG_OBJECT (aacenc, "use RAW format for output");
        params->use_ADTS = 0;
        params->use_ADIF = 0;
      }
    }
  }
  params->noChannels = GST_AUDIO_INFO_CHANNELS (info);
  return ret;
}

static void
gst_ce_aacenc_reset (GstCEAudEnc * ceaudenc)
{
  GstCEAACEnc *aacenc = GST_CE_AACENC (ceaudenc);
  ITTIAM_EAACPLUSENC_Params *aac_params;

  GST_DEBUG_OBJECT (aacenc, "H.264 reset");

  if (ceaudenc->codec_params->size != sizeof (ITTIAM_EAACPLUSENC_Params))
    return;

  ceaudenc->codec_params->bitRate = 32000;
  ceaudenc->codec_params->maxBitRate = 576000;
  ceaudenc->codec_dyn_params->bitRate = 32000;

  aac_params = (ITTIAM_EAACPLUSENC_Params *) ceaudenc->codec_params;

  GST_DEBUG_OBJECT (aacenc, "setup H.264 parameters");
  /* Setting properties defaults */
  aac_params->noChannels = 0;
  aac_params->aacClassic = 1;
  aac_params->psEnable = 0;
  aac_params->dualMono = 0;
  aac_params->downmix = 0;
  aac_params->useSpeechConfig = 0;
  aac_params->fNoStereoPreprocessing = 0;
  aac_params->invQuant = 0;
  aac_params->useTns = 1;
  aac_params->use_ADTS = 0;
  aac_params->use_ADIF = 0;
  aac_params->full_bandwidth = 0;

  /* The following parameters may not be necessary since 
   * they only work for multichannel build 
   */
  aac_params->i_channels_mask = 0x0;
  aac_params->i_num_coupling_chan = 0;
  aac_params->write_program_config_element = 0;

  return;
}

static void
gst_ce_aacenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCEAudEnc *ceaudenc = GST_CEAUDENC (object);
  ITTIAM_EAACPLUSENC_Params *params;

  params = (ITTIAM_EAACPLUSENC_Params *) ceaudenc->codec_params;

  if ((!params) || ceaudenc->codec_handle) {
    GST_WARNING_OBJECT (ceaudenc, "couldn't set property");
    return;
  }

  switch (prop_id) {
    case PROP_DOWNMIX:
      params->downmix = g_value_get_boolean (value) ? 1 : 0;
      break;
    case PROP_STEREO_PREPROCESSING:
      params->fNoStereoPreprocessing = g_value_get_boolean (value) ? 1 : 0;
      break;
    case PROP_INV_QUANT:
      params->invQuant = g_value_get_int (value);
      break;
    case PROP_TNS:
      params->useTns = g_value_get_boolean (value);
      break;
    case PROP_FULL_BANDWIDTH:
      params->full_bandwidth = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  return;
}

static void
gst_ce_aacenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCEAudEnc *ceaudenc = GST_CEAUDENC (object);
  ITTIAM_EAACPLUSENC_Params *params;

  params = (ITTIAM_EAACPLUSENC_Params *) ceaudenc->codec_params;

  if (!params) {
    GST_WARNING_OBJECT (ceaudenc, "couldn't set property");
    return;
  }

  switch (prop_id) {
    case PROP_DOWNMIX:
      g_value_set_boolean (value, params->downmix ? TRUE : FALSE);
      break;
    case PROP_STEREO_PREPROCESSING:
      g_value_set_boolean (value,
          params->fNoStereoPreprocessing ? TRUE : FALSE);
      break;
    case PROP_INV_QUANT:
      g_value_set_int (value, params->invQuant);
      break;
    case PROP_TNS:
      g_value_set_boolean (value, params->useTns ? TRUE : FALSE);
      break;
    case PROP_FULL_BANDWIDTH:
      g_value_set_boolean (value, params->full_bandwidth ? TRUE : FALSE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
