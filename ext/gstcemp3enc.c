/*
 * gstcemp3enc.c
 *
 * Copyright (C) 2013 RidgeRun, LLC (http://www.ridgerun.com)
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

#include <xdc/std.h>
#include <string.h>
#include <ti/sdo/ce/audio1/audenc1.h>
#include <ittiam/codecs/mp3_enc/imp3enc.h>

#include "gstcemp3enc.h"

GST_DEBUG_CATEGORY_STATIC (gst_ce_mp3enc_debug);
#define GST_CAT_DEFAULT gst_ce_mp3enc_debug

#define SAMPLE_RATES "16000, " \
                    "22050, " \
                    "24000, " \
                    "32000, " \
                    "44100, " \
                    "48000"

static GstStaticPadTemplate gst_ce_mp3enc_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) " GST_AUDIO_NE (S16) ", "
        "layout = (string) interleaved, "
        "rate = (int) {" SAMPLE_RATES "}, " "channels=(int)[ 1, 2 ]")
    );

static GstStaticPadTemplate gst_ce_mp3enc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "   mpegversion=(int) { 2 }, "
        "   layer=(int) { 3 }, "
        "   channels= (int)[ 1, 2 ], " "   rate = (int){" SAMPLE_RATES "}")
    );


enum
{
  PROP_0,
  PROP_PACKET,
};


static void gst_ce_mp3enc_reset (GstCeAudEnc * ceaudenc);
static gboolean gst_ce_mp3enc_set_src_caps (GstCeAudEnc * ceaudenc,
    GstAudioInfo * info, GstCaps ** caps, GstBuffer ** codec_data);

static void gst_ce_mp3enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_ce_mp3enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

#define gst_ce_mp3enc_parent_class parent_class
G_DEFINE_TYPE (GstCeMp3Enc, gst_ce_mp3enc, GST_TYPE_CEAUDENC);

static void
gst_ce_mp3enc_class_init (GstCeMp3EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstCeAudEncClass *ceaudenc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  ceaudenc_class = GST_CEAUDENC_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_ce_mp3enc_debug, "ce_mp3enc", 0,
      "CE MP3 encoding element");

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_ce_mp3enc_set_property;
  gobject_class->get_property = gst_ce_mp3enc_get_property;

  g_object_class_install_property (gobject_class, PROP_PACKET,
      g_param_spec_boolean ("packet",
          "Packet",
          "Switch to enable or disable packetization."
          " If this switch is enabled then the encoder "
          "gives constant number of bytes in the output.", FALSE,
          G_PARAM_READWRITE));

  /* pad templates */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ce_mp3enc_sink_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ce_mp3enc_src_pad_template));

  gst_element_class_set_static_metadata (element_class,
      "CE MP3 audio encoder", "Codec/Encoder/Audio",
      "Encode audio in MP3 format",
      "Diego Benavides <diego.benavides@ridgerun.com>");

  ceaudenc_class->codec_name = "mp3enc";
  ceaudenc_class->reset = gst_ce_mp3enc_reset;
  ceaudenc_class->set_src_caps = gst_ce_mp3enc_set_src_caps;

}

static void
gst_ce_mp3enc_init (GstCeMp3Enc * mp3enc)
{
  GstCeAudEnc *ceaudenc = GST_CEAUDENC (mp3enc);
  ITTIAM_MP3ENC_Params *mp3_params = NULL;

  GST_DEBUG_OBJECT (mp3enc, "setup MP3 parameters");
  /* Alloc the params and set a default value */
  mp3_params = g_malloc0 (sizeof (ITTIAM_MP3ENC_Params));
  if (!mp3_params)
    goto fail_alloc;
  *mp3_params = MP3ENCODER_ITTIAM_PARAMS;

  if (ceaudenc->codec_params) {
    GST_DEBUG_OBJECT (mp3enc, "codec params not NULL, copy and free them");
    mp3_params->s_iaudenc_params = *ceaudenc->codec_params;
    g_free (ceaudenc->codec_params);
  }
  ceaudenc->codec_params = (AUDENC1_Params *) mp3_params;

  /* Add the extends params to the original params */
  ceaudenc->codec_params->size = sizeof (ITTIAM_MP3ENC_Params);

  gst_ce_mp3enc_reset (ceaudenc);

  return;
fail_alloc:
  {
    GST_WARNING_OBJECT (ceaudenc, "failed to allocate MP3 params");
    if (mp3_params)
      g_free (mp3_params);
    return;
  }
}

static gboolean
gst_ce_mp3enc_set_src_caps (GstCeAudEnc * ceaudenc, GstAudioInfo * info,
    GstCaps ** caps, GstBuffer ** codec_data)
{
  GstCeMp3Enc *mp3enc = GST_CE_MP3ENC (ceaudenc);
  const gchar *mpegversion = NULL;
  gboolean ret = TRUE;

  ITTIAM_MP3ENC_Params *params;

  mp3enc->channels = GST_AUDIO_INFO_CHANNELS (info);
  GST_INFO_OBJECT (mp3enc, "Set src channels to %i", mp3enc->channels);
  mp3enc->rate = GST_AUDIO_INFO_RATE (info);
  if (mp3enc->rate >= 16000 & mp3enc->rate <= 24000) {
    GST_DEBUG_OBJECT (mp3enc, "Setting samples per frame to 576");
    gst_ce_audenc_set_frame_samples (ceaudenc, 576, 576);
  } else {
    GST_DEBUG_OBJECT (mp3enc, "Setting samples per frame to 1152");
    gst_ce_audenc_set_frame_samples (ceaudenc, 1152, 1152);
  }
  return ret;
}

static void
gst_ce_mp3enc_reset (GstCeAudEnc * ceaudenc)
{
  GstCeMp3Enc *mp3enc = GST_CE_MP3ENC (ceaudenc);
  ITTIAM_MP3ENC_Params *mp3_params;

  GST_DEBUG_OBJECT (mp3enc, "Mp3 reset");

  if (ceaudenc->codec_params->size != sizeof (ITTIAM_MP3ENC_Params)) {
    GST_DEBUG_OBJECT (mp3enc, "Wrong codec_params size for MP3 enc");
    return;
  }

  ceaudenc->codec_params->sampleRate = 44100;
  ceaudenc->codec_params->bitRate = 192000;
  ceaudenc->codec_params->channelMode = IAUDIO_2_0;
  ceaudenc->codec_params->dataEndianness = XDM_LE_16;
  ceaudenc->codec_params->encMode = IAUDIO_CBR;
  ceaudenc->codec_params->inputFormat = IAUDIO_INTERLEAVED;
  ceaudenc->codec_params->inputBitsPerSample = 16;
  ceaudenc->codec_params->crcFlag = XDAS_FALSE;
  ceaudenc->codec_params->ancFlag = XDAS_FALSE;
  ceaudenc->codec_params->lfeFlag = XDAS_FALSE;

  ceaudenc->codec_dyn_params->bitRate = ceaudenc->codec_params->bitRate;
  ceaudenc->codec_dyn_params->sampleRate = ceaudenc->codec_params->sampleRate;
  ceaudenc->codec_dyn_params->channelMode = ceaudenc->codec_params->channelMode;
  ceaudenc->codec_dyn_params->lfeFlag = ceaudenc->codec_params->lfeFlag;
  ceaudenc->codec_dyn_params->inputBitsPerSample =
      ceaudenc->codec_params->inputBitsPerSample;

  mp3_params = (ITTIAM_MP3ENC_Params *) ceaudenc->codec_params;
  mp3_params->packet = 0;

  mp3enc->rate = ceaudenc->codec_params->sampleRate;
  mp3enc->channels = 2;

  return;
}

static void
gst_ce_mp3enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCeAudEnc *ceaudenc = GST_CEAUDENC (object);
  ITTIAM_MP3ENC_Params *params;

  params = (ITTIAM_MP3ENC_Params *) ceaudenc->codec_params;

  if ((!params) || ceaudenc->codec_handle) {
    GST_WARNING_OBJECT (ceaudenc, "couldn't set property");
    return;
  }

  switch (prop_id) {
    case PROP_PACKET:
      params->packet = g_value_get_boolean (value) ? 1 : 0;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  return;
}

static void
gst_ce_mp3enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCeAudEnc *ceaudenc = GST_CEAUDENC (object);
  ITTIAM_MP3ENC_Params *params;

  params = (ITTIAM_MP3ENC_Params *) ceaudenc->codec_params;

  if (!params) {
    GST_WARNING_OBJECT (ceaudenc, "couldn't get property");
    return;
  }

  switch (prop_id) {
    case PROP_PACKET:
      g_value_set_boolean (value, params->packet ? TRUE : FALSE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
