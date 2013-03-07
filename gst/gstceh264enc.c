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
#include "gstcevidenc.h"


GstStaticCaps gst_ce_h264enc_sink_caps = GST_STATIC_CAPS ("video/x-raw, "
    "   format = (string) NV12,"
    "   framerate=(fraction)[ 0, 120], "
    "   width=(int)[ 128, 4080 ], " "   height=(int)[ 96, 4096 ]");

GstStaticCaps gst_ce_h264enc_src_caps = GST_STATIC_CAPS ("video/x-h264, "
    "   framerate=(fraction)[ 0, 120], "
    "   width=(int)[ 128, 4080 ], "
    "   height=(int)[ 96, 4096 ],"
    "   stream-format = (string) { avc, byte-stream }");

enum
{
  GST_CE_H264ENC_STREAM_FORMAT_AVC,
  GST_CE_H264ENC_STREAM_FORMAT_BYTE_STREAM,
  GST_CE_H264ENC_STREAM_FORMAT_FROM_PROPERTY
};

typedef struct
{
  gint current_stream_format;
  gboolean byte_stream;
} h264PrivateData;

static gboolean
gst_ce_h264enc_set_src_caps (GObject * object, GstCaps ** caps)
{
  GstCEVidEnc *cevidenc = (GstCEVidEnc *) (object);
  h264PrivateData *h264enc;
  GstStructure *s;
  const gchar *stream_format;

  GST_DEBUG_OBJECT (cevidenc, "setting H.264 caps");
  if (!cevidenc->codec_private)
    goto no_private_data;

  h264enc = cevidenc->codec_private;
  gst_caps_unref (*caps);
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
    gst_structure_set (s, "stream-format", G_TYPE_STRING, "avc", NULL);
  } else {
    gst_structure_set (s, "stream-format", G_TYPE_STRING, "byte-stream", NULL);
  }
  GST_DEBUG_OBJECT ("H.264 caps %s", gst_caps_to_string (*caps));

  return TRUE;

no_private_data:
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
  IH264VENC_Params *h264_params;
  IH264VENC_DynamicParams *h264_dyn_params;

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

  h264enc->byte_stream = 1;

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

GstCECodecData gst_ce_h264enc = {
  .name = "h264enc",
  .long_name = "H.264",
  .src_caps = &gst_ce_h264enc_src_caps,
  .sink_caps = &gst_ce_h264enc_sink_caps,
  .setup = gst_ce_h264enc_setup,
  .set_src_caps = gst_ce_h264enc_set_src_caps,
  .install_properties = NULL,
  .set_property = NULL,
  .get_property = NULL,
};
