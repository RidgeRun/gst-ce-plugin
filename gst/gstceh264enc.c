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
  gint header_size;
} h264PrivateData;

typedef struct
{
  gint type;
  gint index;
  gint size;
} nalUnit;


static gboolean
gst_ce_h264_fetch_header (guint8 * data, gint buffer_size,
    nalUnit * sps, nalUnit * pps)
{
  gint i;
  gint nal_type;
  gint found = 0;
  nalUnit *nalu = NULL;


  GST_DEBUG ("fetching header");
  GST_MEMDUMP ("Header", data, buffer_size);
  for (i = 0; i < (buffer_size - 5); i++) {
    /* In bytestream format each NAL si preceded by 
     * a four byte start code: 0x00 0x00 0x00 0x01.
     * The byte after this code indicates the NAL type,
     * we're looking for the SPS(0x07) and PPS(0x08) NAL*/
    if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0
        && data[i + 3] == 1) {

      if (nalu) {
        nalu->size = i - nalu->index;
        nalu = NULL;
      }

      if (found == 2)
        break;

      nal_type = (data[i + 4]) & 0x1f;
      if (nal_type == 0x07)
        nalu = sps;
      else if (nal_type == 0x08)
        nalu = pps;
      else
        continue;

      nalu->type = nal_type;
      nalu->index = i + 4;

      i += 4;
      found++;
    }
  }

  if (i >= (buffer_size - 5))
    nalu->size = buffer_size - nalu->index;

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
    goto control_params_fail;

  /*Allocate an output buffer for the header */
  header_buf = gst_buffer_new_allocate (cevidenc->allocator, 200,
      &cevidenc->params);
  if (!gst_buffer_map (header_buf, &info, GST_MAP_WRITE))
    return FALSE;

  cevidenc->outbuf.bufs = (XDAS_Int8 **) & (info.data);

  /* Set output and input arguments for the encode process */
  in_args.size = sizeof (IVIDENC1_InArgs);
  in_args.inputID = 1;
  in_args.topFieldFirstFlag = 1;

  out_args.size = sizeof (VIDENC1_OutArgs);

  /* Generate the header */
  ret =
      VIDENC1_process (cevidenc->codec_handle, &cevidenc->inbuf,
      &cevidenc->outbuf, &in_args, &out_args);
  if (ret != VIDENC1_EOK)
    goto encode_fail;

  gst_buffer_unmap (header_buf, &info);

  cevidenc->codec_dyn_params->generateHeader = XDM_ENCODE_AU;
  ret = VIDENC1_control (cevidenc->codec_handle, XDM_SETPARAMS,
      cevidenc->codec_dyn_params, &enc_status);
  if (ret != VIDENC1_EOK)
    goto control_params_fail;

  h264enc->header_size = out_args.bytesGenerated;
  *buf = header_buf;

  return TRUE;

control_params_fail:
  {
    GST_WARNING_OBJECT (cevidenc, "Failed to set dynamic parameters, "
        "status error %x, %d", (unsigned int) enc_status.extendedError, ret);
    return FALSE;
  }
encode_fail:
  {
    GST_WARNING_OBJECT (cevidenc,
        "Failed header encode process with extended error: 0x%x",
        (unsigned int) out_args.extendedError);
    return FALSE;
  }

}

static gboolean
gst_ce_h264_get_codec_data (GstCEVidEnc * cevidenc)
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

  if (!gst_ce_h264enc_get_header (cevidenc, &buf))
    return FALSE;

  if (!gst_buffer_map (buf, &info, GST_MAP_READ))
    return FALSE;

  header = info.data;
  gst_ce_h264_fetch_header (header, h264enc->header_size, &sps, &pps);

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
  codec_data_size += sps.size + pps.size + 11;
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

  buffer[nal_idx++] = 1;        /* number of PPSs */
  GST_WRITE_UINT16_BE (buffer + nal_idx, pps.size);
  nal_idx += 2;
  memcpy (buffer + nal_idx, &header[pps.index], pps.size);
  nal_idx += pps.size;

  GST_MEMDUMP ("Codec data", buffer, codec_data_size);
  gst_buffer_unref (buf);
  g_free (buffer);
}

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
    gst_ce_h264_get_codec_data (cevidenc);
    gst_structure_set (s, "stream-format", G_TYPE_STRING, "avc", NULL);
  } else {
    gst_structure_set (s, "stream-format", G_TYPE_STRING, "byte-stream", NULL);
  }
  GST_DEBUG_OBJECT (cevidenc, "H.264 caps %s", gst_caps_to_string (*caps));

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
