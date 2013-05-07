/* GStreamer
 *
 * unit test for ce_aacenc
 * Copyright (C) <2013> RidgeRun
 * 
 * Modified faac unit test to use the ce_aacenc
 * Copyright (C) <2009> Mark Nauwelaerts <mnauw@users.sf.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <unistd.h>

#include <gst/check/gstcheck.h>
#include <gst/audio/audio.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;

#define AUDIO_CAPS_STRING "audio/x-raw, " \
                           "format = (string) " GST_AUDIO_NE (S16) ", "\
                           "layout = (string) interleaved, " \
                           "rate = (int) 48000, " \
                           "channels = (int) 2, " \
                           "channel-mask = (bitmask) 3"

#define AAC_RAW_CAPS_STRING "audio/mpeg, " \
                          "mpegversion = (int) 4, " \
                          "rate = (int) 48000, " \
                          "channels = (int) 2, " \
                          "stream-format = \"raw\"," \
                          "base-profile = \"lc\""

#define AAC_ADTS_CAPS_STRING "audio/mpeg, " \
                          "mpegversion = (int) 4, " \
                          "rate = (int) 48000, " \
                          "channels = (int) 2, " \
                          "stream-format = \"adts\"," \
                          "base-profile = \"lc\""

static GstStaticPadTemplate sinktemplate_adts = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (AAC_ADTS_CAPS_STRING));

static GstStaticPadTemplate sinktemplate_raw = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (AAC_RAW_CAPS_STRING));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (AUDIO_CAPS_STRING));


static GstElement *
setup_ce_aacenc (gboolean adts)
{
  GstElement *aacenc;

  GST_DEBUG ("setup_ce_aacenc");
  aacenc = gst_check_setup_element ("ce_aacenc");
  mysrcpad = gst_check_setup_src_pad (aacenc, &srctemplate);

  if (adts)
    mysinkpad = gst_check_setup_sink_pad (aacenc, &sinktemplate_adts);
  else
    mysinkpad = gst_check_setup_sink_pad (aacenc, &sinktemplate_raw);

  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return aacenc;
}

static void
cleanup_ce_aacenc (GstElement * aacenc)
{
  GST_DEBUG ("cleanup_aacenc");
  gst_element_set_state (aacenc, GST_STATE_NULL);

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (aacenc);
  gst_check_teardown_sink_pad (aacenc);
  gst_check_teardown_element (aacenc);
}

static void
do_test (gboolean adts)
{
  GstElement *aacenc;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint i, num_buffers;
  const gint nbuffers = 2;

  aacenc = setup_ce_aacenc (adts);
  fail_unless (gst_element_set_state (aacenc,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  /* corresponds to audio buffer mentioned in the caps */
  inbuffer = gst_buffer_new_and_alloc (1024 * nbuffers * 2 * 2);
  /* makes valgrind's memcheck happier */
  gst_buffer_memset (inbuffer, 0, 0, 1024 * nbuffers * 2 * 2);
  caps = gst_caps_from_string (AUDIO_CAPS_STRING);
  gst_pad_set_caps (mysrcpad, caps);
  gst_caps_unref (caps);
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  GST_INFO ("Hola");
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  GST_DEBUG ("Adios");
  /* send eos to have all flushed if needed */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()) == TRUE);
  GST_DEBUG ("Flush %p", buffers);

  num_buffers = g_list_length (buffers);
  fail_unless_equals_int (num_buffers, nbuffers);
  GST_DEBUG ("Num buffers %d", num_buffers);
  /* clean up buffers */
  for (i = 0; i < num_buffers; ++i) {
    gint header = 0, id;
    GstMapInfo map;
    gsize size;
    guint8 *data;

    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);

    gst_buffer_map (outbuffer, &map, GST_MAP_READ);
    data = map.data;
    size = map.size;

    if (adts) {
      gboolean protection;
      gint k;

      fail_if (size < 7);
      protection = !(data[1] & 0x1);
      /* expect only 1 raw data block */
      k = (data[6] & 0x3) + 1;
      fail_if (k != 1);

      header = 7;
      if (protection)
        header += (k - 1) * 2 + 2;

      /* check header */
      k = GST_READ_UINT16_BE (data) & 0xFFF6;
      /* sync */
      fail_unless (k == 0xFFF0);
      k = data[2];
      /* profile */
      fail_unless ((k >> 6) == 0x1);
      /* rate */
      fail_unless (((k >> 2) & 0xF) == 0x3);
      /* channels */
      fail_unless ((k & 0x1) == 0);
      k = data[3];
      fail_unless ((k >> 6) == 0x2);
    } else {
      GstCaps *caps;
      GstStructure *s;
      const GValue *value;
      GstBuffer *buf;
      gint k;
      GstMapInfo cmap;

      caps = gst_pad_get_current_caps (mysinkpad);
      fail_if (caps == NULL);
      s = gst_caps_get_structure (caps, 0);
      fail_if (s == NULL);
      value = gst_structure_get_value (s, "codec_data");
      fail_if (value == NULL);
      buf = gst_value_get_buffer (value);
      fail_if (buf == NULL);
      gst_buffer_map (buf, &cmap, GST_MAP_READ);
      fail_if (cmap.size < 2);
      k = GST_READ_UINT16_BE (cmap.data);
      gst_buffer_unmap (buf, &cmap);
      /* profile, rate, channels */
      fail_unless ((k & 0xFFF8) == ((0x02 << 11) | (0x3 << 7) | (0x02 << 3)));
      gst_caps_unref (caps);
    }

    fail_if (size <= header);
    id = data[header] & (0x7 << 5);
    /* allow all but ID_END or ID_LFE */
    fail_if (id == 7 || id == 3);
    gst_buffer_unmap (outbuffer, &map);

    buffers = g_list_remove (buffers, outbuffer);

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  cleanup_ce_aacenc (aacenc);
  g_list_free (buffers);
  buffers = NULL;
}

GST_START_TEST (test_adts)
{
  do_test (TRUE);
}

GST_END_TEST;

GST_START_TEST (test_raw)
{
  do_test (FALSE);
}

GST_END_TEST;

static Suite *
ce_aacenc_suite (void)
{
  Suite *s = suite_create ("ce_aacenc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_adts);
  tcase_add_test (tc_chain, test_raw);

  return s;
}

GST_CHECK_MAIN (ce_aacenc);