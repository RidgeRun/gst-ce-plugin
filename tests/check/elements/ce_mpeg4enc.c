/*
 * unit test for ce_mpeg4enc
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

#define MPEG4_CAPS_STRING "video/mpeg, " \
                           "width = (int) 640, " \
                           "height = (int) 480, " \
                           "framerate = (fraction) 30/1"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (MPEG4_CAPS_STRING));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS_STRING));

static GstElement *
setup_ce_mpeg4enc (GstStaticPadTemplate * sinktemplate)
{
  GstElement *mpeg4enc;

  GST_DEBUG ("setup ce_mpeg4enc");
  mpeg4enc = gst_check_setup_element ("ce_mpeg4enc");
  mysinkpad = gst_check_setup_sink_pad (mpeg4enc, sinktemplate);
  mysrcpad = gst_check_setup_src_pad (mpeg4enc, &srctemplate);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return mpeg4enc;
}

static void
cleanup_ce_mpeg4enc (GstElement * mpeg4enc)
{
  GST_DEBUG ("cleanup mpeg4enc");
  gst_element_set_state (mpeg4enc, GST_STATE_NULL);

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (mpeg4enc);
  gst_check_teardown_sink_pad (mpeg4enc);
  gst_check_teardown_element (mpeg4enc);
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
play_a_buffer (GstElement * mpeg4enc, GstCaps * caps)
{
  GstBuffer *inbuffer;
  fail_unless (gst_element_set_state (mpeg4enc,
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
check_caps (GstCaps * caps, gint level)
{
  GstStructure *s;
  GstMapInfo map;
  GstBuffer *buf;
  const GValue *sf;
  gint version, start_code;

  fail_unless (caps != NULL);

  GST_INFO ("caps %" GST_PTR_FORMAT, caps);
  s = gst_caps_get_structure (caps, 0);
  fail_unless (s != NULL);
  fail_if (!gst_structure_has_name (s, "video/mpeg"));

  /* Check mpeg version */
  sf = gst_structure_get_value (s, "mpegversion");
  fail_unless (sf != NULL);
  fail_unless (G_VALUE_HOLDS_INT (sf));
  version = g_value_get_int (sf);
  fail_unless (version == 4);
  /* Check codec data */
  sf = gst_structure_get_value (s, "codec_data");
  fail_unless (sf != NULL);
  fail_unless (GST_VALUE_HOLDS_BUFFER (sf));
  buf = gst_value_get_buffer (sf);
  fail_unless (buf != NULL);
  gst_buffer_map (buf, &map, GST_MAP_READ);

  start_code = GST_READ_UINT32_BE (map.data);
  fail_unless (start_code == 0x1b0);

  GST_DEBUG ("Level %d", map.data[4]);
  fail_unless (map.data[4] == level);
  gst_buffer_unmap (buf, &map);

}

GST_START_TEST (test_ce_mpeg4enc_properties)
{
  GstElement *mpeg4enc;
  GstCaps *caps;
  gint res_rate, res_level, res_bitrate;

  mpeg4enc = setup_ce_mpeg4enc (&sinktemplate);

  caps = gst_caps_from_string (MPEG4_CAPS_STRING);

  g_object_set (mpeg4enc, "rate-control", 2, NULL);
  g_object_set (mpeg4enc, "level", 4, NULL);
  g_object_set (mpeg4enc, "target-bitrate", 4000000, NULL);

  g_object_get (mpeg4enc,
      "rate-control", &res_rate,
      "level", &res_level, "target-bitrate", &res_bitrate, NULL);

  fail_unless (res_rate == 2);
  fail_unless (res_level == 4);
  fail_unless (res_bitrate == 4000000);

  play_a_buffer (mpeg4enc, caps);

  /* check output caps */
  {
    GstCaps *outcaps;

    outcaps = gst_pad_get_current_caps (mysinkpad);
    check_caps (outcaps, 4);
    gst_caps_unref (outcaps);
  }


  /* change dynamic properties while in PLAYING state */
  g_object_set (mpeg4enc, "target-bitrate", 1000000, NULL);
  g_object_get (mpeg4enc, "target-bitrate", &res_bitrate, NULL);

  fail_unless (res_bitrate == 1000000);

  /* try to change static properties while in PLAYING state */
  g_object_set (mpeg4enc, "rate-control", 5, NULL);
  g_object_get (mpeg4enc, "rate-control", &res_rate, NULL);

  /* verify that static properties have not been altered */
  fail_unless (res_rate == 2);

  /* send eos to have all flushed if needed */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()) == TRUE);


  cleanup_ce_mpeg4enc (mpeg4enc);
  g_list_free (buffers);
  buffers = NULL;

}

GST_END_TEST;

GST_START_TEST (test_ce_mpeg4enc_stream)
{
  GstElement *mpeg4enc;
  GstBuffer *outbuffer;
  GstCaps *caps;
  int i, num_buffers;

  mpeg4enc = setup_ce_mpeg4enc (&sinktemplate);

  caps = gst_caps_from_string (MPEG4_CAPS_STRING);

  play_a_buffer (mpeg4enc, caps);

  /* send eos to have all flushed if needed */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()) == TRUE);

  num_buffers = g_list_length (buffers);
  fail_unless (num_buffers == 1);

  /* check output caps */
  {
    GstCaps *outcaps;

    outcaps = gst_pad_get_current_caps (mysinkpad);
    check_caps (outcaps, 5);
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
        fail_unless (start_code == 0x1b0);

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

  cleanup_ce_mpeg4enc (mpeg4enc);
  g_list_free (buffers);
  buffers = NULL;
}

GST_END_TEST;

static Suite *
ce_mpeg4enc_suite (void)
{
  Suite *s = suite_create ("ce_mpeg4enc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_ce_mpeg4enc_stream);
  tcase_add_test (tc_chain, test_ce_mpeg4enc_properties);

  return s;
}

GST_CHECK_MAIN (ce_mpeg4enc);
