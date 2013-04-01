/*
 * unit test for ce_h264enc
 *
 * Copyright (C) 2013 RidgeRun, LLC (http://www.ridgerun.com)
 *
 * Based on x264enc unit test
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

#include <unistd.h>

#include <gst/cmem/gstcmemallocator.h>
#include <gst/check/gstcheck.h>
#include <gst/app/gstappsink.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;

#define VIDEO_CAPS_STRING "video/x-raw, " \
                           "format = (string) NV12, " \
                           "width = (int) 640, " \
                           "height = (int) 480, " \
                           "framerate = (fraction) 30/1"

#define H264_CAPS_STRING "video/x-h264, " \
                           "width = (int) 640, " \
                           "height = (int) 480, " \
                           "framerate = (fraction) 30/1"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (H264_CAPS_STRING));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS_STRING));

static GstElement *
setup_ce_h264enc (GstStaticPadTemplate * sinktemplate)
{
  GstElement *h264enc;

  GST_DEBUG ("setup ce_h264enc");
  h264enc = gst_check_setup_element ("ce_h264enc");
  mysinkpad = gst_check_setup_sink_pad (h264enc, sinktemplate);
  mysrcpad = gst_check_setup_src_pad (h264enc, &srctemplate);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return h264enc;
}

static void
cleanup_ce_h264enc (GstElement * h264enc)
{
  GST_DEBUG ("cleanup h264enc");
  gst_element_set_state (h264enc, GST_STATE_NULL);

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (h264enc);
  gst_check_teardown_sink_pad (h264enc);
  gst_check_teardown_element (h264enc);
}

static GstBuffer *
create_video_buffer (GstCaps * caps)
{
  GstElement *pipeline;
  GstElement *cf;
  GstElement *sink;
  GstSample *sample;
  GstBuffer *buffer;

  pipeline =
      gst_parse_launch
      ("v4l2src num-buffers=1 ! capsfilter name=cf ! appsink name=sink", NULL);
  g_assert (pipeline != NULL);

  cf = gst_bin_get_by_name (GST_BIN (pipeline), "cf");
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");

  g_object_set (G_OBJECT (cf), "caps", caps, NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  sample = gst_app_sink_pull_sample (GST_APP_SINK (sink));

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  gst_object_unref (sink);
  gst_object_unref (cf);

  buffer = gst_sample_get_buffer (sample);
  gst_buffer_ref (buffer);

  gst_sample_unref (sample);

  return buffer;
}

static GstBuffer *
create_cmem_buffer (gint size)
{
  GstAllocator *alloc;
  GstAllocationParams params;
  GstBuffer *buf;

  /* memory using the cmem API */
  gst_cmem_init ();

  alloc = gst_allocator_find ("ContiguousMemory");
  fail_unless (alloc != NULL);

  gst_allocation_params_init (&params);
  buf = gst_buffer_new_allocate (alloc, size, &params);
  gst_buffer_memset (buf, 0, 0, -1);
  GST_BUFFER_TIMESTAMP (buf) = 0;

  gst_object_unref (alloc);

  return buf;
}

static void
play_a_buffer (GstElement * h264enc, GstCaps * caps)
{
  GstBuffer *inbuffer;
  fail_unless (gst_element_set_state (h264enc,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  gst_pad_set_caps (mysinkpad, caps);
  gst_caps_unref (caps);
  gst_pad_use_fixed_caps (mysinkpad);

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  fail_unless (gst_pad_set_caps (mysrcpad, caps));

  fail_unless ((inbuffer = create_cmem_buffer (640 * 480 * 3 / 2)) != NULL);
  gst_caps_unref (caps);

  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);

}

static void
check_caps (GstCaps * caps, gint profile_id)
{
  GstStructure *s;
  const GValue *sf, *avcc;
  const gchar *stream_format;

  fail_unless (caps != NULL);

  GST_INFO ("caps %" GST_PTR_FORMAT, caps);
  s = gst_caps_get_structure (caps, 0);
  fail_unless (s != NULL);
  fail_if (!gst_structure_has_name (s, "video/x-h264"));
  sf = gst_structure_get_value (s, "stream-format");
  fail_unless (sf != NULL);
  fail_unless (G_VALUE_HOLDS_STRING (sf));
  stream_format = g_value_get_string (sf);
  fail_unless (stream_format != NULL);
  if (strcmp (stream_format, "avc") == 0) {
    GstMapInfo map;
    GstBuffer *buf;

    avcc = gst_structure_get_value (s, "codec_data");
    fail_unless (avcc != NULL);
    fail_unless (GST_VALUE_HOLDS_BUFFER (avcc));
    buf = gst_value_get_buffer (avcc);
    fail_unless (buf != NULL);
    gst_buffer_map (buf, &map, GST_MAP_READ);
    fail_unless_equals_int (map.data[0], 1);
    GST_DEBUG ("Profile ID %d", map.data[1]);
    fail_unless (map.data[1] == profile_id);
    gst_buffer_unmap (buf, &map);
  } else if (strcmp (stream_format, "byte-stream") == 0) {
    fail_if (gst_structure_get_value (s, "codec_data") != NULL);
  } else {
    fail_if (TRUE, "unexpected stream-format in caps: %s", stream_format);
  }
}

GST_START_TEST (test_ce_h264enc_properties)
{
  GstElement *h264enc;
  GstCaps *caps;
  gint res_rate, res_level, res_bitrate, res_idrinterval;

  h264enc = setup_ce_h264enc (&sinktemplate);

  caps = gst_caps_from_string (H264_CAPS_STRING);
  gst_caps_set_simple (caps, "stream-format", G_TYPE_STRING, "avc", NULL);

  g_object_set (h264enc, "rate-control", 2, NULL);
  g_object_set (h264enc, "level", 10, NULL);
  g_object_set (h264enc, "target-bitrate", 4000000, NULL);
  g_object_set (h264enc, "idrinterval", 90, NULL);

  g_object_get (h264enc,
      "rate-control", &res_rate,
      "level", &res_level,
      "target-bitrate", &res_bitrate, "idrinterval", &res_idrinterval, NULL);

  fail_unless (res_rate == 2);
  fail_unless (res_level == 10);
  fail_unless (res_bitrate == 4000000);
  fail_unless (res_idrinterval == 90);

  play_a_buffer (h264enc, caps);

  /* change dynamic properties while in PLAYING state */
  g_object_set (h264enc, "target-bitrate", 1000000, NULL);
  g_object_set (h264enc, "idrinterval", 30, NULL);
  g_object_get (h264enc,
      "target-bitrate", &res_bitrate, "idrinterval", &res_idrinterval, NULL);

  fail_unless (res_bitrate == 1000000);
  fail_unless (res_idrinterval == 30);

  /* try to change static properties while in PLAYING state */
  g_object_set (h264enc, "rate-control", 3, NULL);
  g_object_get (h264enc, "rate-control", &res_rate, NULL);

  /* verify that static properties have not been altered */
  fail_unless (res_rate == 2);

  /* send eos to have all flushed if needed */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()) == TRUE);


  cleanup_ce_h264enc (h264enc);
  g_list_free (buffers);
  buffers = NULL;

}

GST_END_TEST;

GST_START_TEST (test_ce_h264enc_bytestream)
{
  GstElement *h264enc;
  GstBuffer *outbuffer;
  GstCaps *caps;
  int i, num_buffers;

  h264enc = setup_ce_h264enc (&sinktemplate);

  caps = gst_caps_from_string (H264_CAPS_STRING);
  gst_caps_set_simple (caps, "stream-format", G_TYPE_STRING, "byte-stream",
      NULL);

  play_a_buffer (h264enc, caps);

  /* send eos to have all flushed if needed */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()) == TRUE);

  num_buffers = g_list_length (buffers);
  fail_unless (num_buffers == 1);

  /* check output caps */
  {
    GstCaps *outcaps;

    outcaps = gst_pad_get_current_caps (mysinkpad);
    check_caps (outcaps, 0);
    gst_caps_unref (outcaps);
  }

  /* clean up buffers */
  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);

    switch (i) {
      case 0:
      {
        gint start_code;
        GstMapInfo map;
        const guint8 *data;

        gst_buffer_map (outbuffer, &map, GST_MAP_READ);
        data = map.data;

        start_code = GST_READ_UINT32_BE (data);
        fail_unless (start_code == 1);

        gst_buffer_unmap (outbuffer, &map);
        break;
      }
      default:
        break;
    }

    buffers = g_list_remove (buffers, outbuffer);

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  cleanup_ce_h264enc (h264enc);
  g_list_free (buffers);
  buffers = NULL;
}

GST_END_TEST;

static void
test_video_packetized (gboolean headers, gint profile_id)
{
  GstElement *h264enc;
  GstBuffer *outbuffer;
  GstCaps *caps;
  int i, num_buffers;

  h264enc = setup_ce_h264enc (&sinktemplate);
  /* Setting properties that allows any profile */
  g_object_set (h264enc, "entropy", 0, NULL);
  g_object_set (h264enc, "t8x8intra", FALSE, NULL);
  g_object_set (h264enc, "seqscaling", 0, NULL);
  /* Setting defined profile */
  g_object_set (h264enc, "profile", profile_id, NULL);
  g_object_set (h264enc, "headers", headers, NULL);

  caps = gst_caps_from_string (H264_CAPS_STRING);
  /* code below assumes avc */
  gst_caps_set_simple (caps, "stream-format", G_TYPE_STRING, "avc", NULL);

  play_a_buffer (h264enc, caps);

  num_buffers = g_list_length (buffers);
  fail_unless (num_buffers == 1);

  /* send eos to have all flushed if needed */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()) == TRUE);

  /* check output caps */
  {
    GstCaps *outcaps;

    outcaps = gst_pad_get_current_caps (mysinkpad);
    check_caps (outcaps, profile_id);
    gst_caps_unref (outcaps);
  }

  /* clean up buffers */
  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);

    switch (i) {
      case 0:
      {
        gint nsize, npos, j, type, next_type;
        GstMapInfo map;
        const guint8 *data;
        gsize size;

        gst_buffer_map (outbuffer, &map, GST_MAP_READ);
        data = map.data;
        size = map.size;

        npos = 0;
        j = 0;
        if (headers) {
          /* need SPS first */
          next_type = 7;
        } else {
          /* buffer has just data */
          next_type = 5;
        }
        /* loop through NALs */
        while (npos < size) {
          fail_unless (size - npos >= 4);
          nsize = GST_READ_UINT32_BE (data + npos);
          fail_unless (nsize > 0);
          fail_unless (npos + 4 + nsize <= size);
          type = data[npos + 4] & 0x1F;
          /* check the first NALs, disregard AU (9), SEI (6) */
          if (type != 9 && type != 6) {
            fail_unless (type == next_type);
            switch (type) {
              case 7:
                /* SPS */
                next_type = 8;
                break;
              case 8:
                /* PPS */
                next_type = 5;
                break;
              default:
                break;
            }
            j++;
          }
          npos += nsize + 4;
        }
        gst_buffer_unmap (outbuffer, &map);
        /* should have reached the exact end */
        fail_unless (npos == size);
        break;
      }
      default:
        break;
    }

    buffers = g_list_remove (buffers, outbuffer);

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  cleanup_ce_h264enc (h264enc);
  g_list_free (buffers);
  buffers = NULL;
}

GST_START_TEST (test_ce_h264enc_packetized_high)
{
  /*Test high profile with headers */
  test_video_packetized (TRUE, 100);
  /*Test high profile without headers */
  test_video_packetized (FALSE, 100);
}

GST_END_TEST;

GST_START_TEST (test_ce_h264enc_packetized_main)
{
  test_video_packetized (FALSE, 66);
}

GST_END_TEST;

GST_START_TEST (test_ce_h264enc_packetized_base)
{
  test_video_packetized (FALSE, 77);
}

GST_END_TEST;

static Suite *
ce_h264enc_suite (void)
{
  Suite *s = suite_create ("ce_h264enc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_ce_h264enc_packetized_high);
  tcase_add_test (tc_chain, test_ce_h264enc_packetized_main);
  tcase_add_test (tc_chain, test_ce_h264enc_packetized_base);
  tcase_add_test (tc_chain, test_ce_h264enc_bytestream);
  tcase_add_test (tc_chain, test_ce_h264enc_properties);

  return s;
}

GST_CHECK_MAIN (ce_h264enc);
