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
#include <ti/sdo/codecs/mpeg4enc/imp4venc.h>

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
};


typedef struct
{

} mpeg4PrivateData;

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


  /* pad templates */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ce_mpeg4enc_sink_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ce_mpeg4enc_src_pad_template));

  gst_element_class_set_static_metadata (element_class,
      "CE MPEG-4 video encoder", "Codec/Encoder/Video",
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

  GST_DEBUG_OBJECT (mpeg4enc, "setup MPEG-4 parameters");

  gst_ce_mpeg4enc_reset (ce_videnc);

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
  GstStructure *s;
  const gchar *stream_format;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (mpeg4enc, "setting MPEG-4 caps");

  return gst_ce_mpeg4enc_get_codec_data (mpeg4enc, codec_data);
}

static void
gst_ce_mpeg4enc_reset (GstCeVidEnc * ce_videnc)
{
  GstCeMpeg4Enc *mpeg4enc = GST_CE_MPEG4ENC (ce_videnc);

  GST_DEBUG_OBJECT (mpeg4enc, "MPEG-4 reset");

  return;
}


static void
gst_ce_mpeg4enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCeVidEnc *ce_videnc = GST_CEVIDENC (object);
  GstCeMpeg4Enc *mpeg4enc = GST_CE_MPEG4ENC (object);


  return;

}

static void
gst_ce_mpeg4enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCeVidEnc *ce_videnc = GST_CEVIDENC (object);
  GstCeMpeg4Enc *mpeg4enc = GST_CE_MPEG4ENC (object);
}
