/*
 * unit test for ce_jpegenc
 *
 * Copyright (C) 2013 RidgeRun, LLC (http://www.ridgerun.com)
 *
 * Based on jpegenc unit test
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

#define JPEG_CAPS_RESTRICTIVE  "image/jpeg, " \
                           "width = (int) [128, 4080], " \
                           "height = (int) [96,4096], " \
                           "framerate = (fraction) 30/1"

#define JPEG_CAPS_STRING "image/jpeg"

static GstStaticPadTemplate jpeg_sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (JPEG_CAPS_STRING));

static GstStaticPadTemplate any_sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate jpeg_restrictive_sinktemplate =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (JPEG_CAPS_RESTRICTIVE));

static GstStaticPadTemplate any_srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS_STRING));

static GstElement *
setup_ce_jpegenc (GstStaticPadTemplate * sinktemplate)
{
  GstElement *jpegenc;

  GST_DEBUG ("setup ce_jpegenc");
  jpegenc = gst_check_setup_element ("ce_jpegenc");
  mysinkpad = gst_check_setup_sink_pad (jpegenc, sinktemplate);
  mysrcpad = gst_check_setup_src_pad (jpegenc, &any_srctemplate);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return jpegenc;
}

static void
cleanup_ce_jpegenc (GstElement * jpegenc)
{
  GST_DEBUG ("cleanup jpegenc");
  gst_element_set_state (jpegenc, GST_STATE_NULL);

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (jpegenc);
  gst_check_teardown_sink_pad (jpegenc);
  gst_check_teardown_element (jpegenc);
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

GST_START_TEST (test_ce_jpegenc_getcaps)
{
  GstElement *jpegenc;
  GstPad *sinkpad;
  GstCaps *caps;
  GstStructure *structure;
  gint fps_n;
  gint fps_d;
  const GValue *value;

  /* we are going to do some get caps to confirm it doesn't return non-subset
   * caps */

  jpegenc = setup_ce_jpegenc (&any_sinktemplate);
  sinkpad = gst_element_get_static_pad (jpegenc, "sink");
  /* this should assert if non-subset */
  caps = gst_pad_query_caps (sinkpad, NULL);
  gst_caps_unref (caps);
  gst_object_unref (sinkpad);
  cleanup_ce_jpegenc (jpegenc);

  jpegenc = setup_ce_jpegenc (&jpeg_sinktemplate);
  sinkpad = gst_element_get_static_pad (jpegenc, "sink");
  /* this should assert if non-subset */
  caps = gst_pad_query_caps (sinkpad, NULL);
  gst_caps_unref (caps);
  gst_object_unref (sinkpad);
  cleanup_ce_jpegenc (jpegenc);

  /* now use a more restricted one and check the resulting caps */
  jpegenc = setup_ce_jpegenc (&jpeg_restrictive_sinktemplate);
  sinkpad = gst_element_get_static_pad (jpegenc, "sink");
  /* this should assert if non-subset */
  caps = gst_pad_query_caps (sinkpad, NULL);
  structure = gst_caps_get_structure (caps, 0);

  /* check the width */
  value = gst_structure_get_value (structure, "width");
  fail_unless (gst_value_get_int_range_min (value) == 128);
  fail_unless (gst_value_get_int_range_max (value) == 4080);

  value = gst_structure_get_value (structure, "height");
  fail_unless (gst_value_get_int_range_min (value) == 96);
  fail_unless (gst_value_get_int_range_max (value) == 4096);

  fail_unless (gst_structure_get_fraction (structure, "framerate", &fps_n,
          &fps_d));
  fail_unless (fps_n == 30);
  fail_unless (fps_d == 1);

  gst_caps_unref (caps);
  gst_object_unref (sinkpad);
  cleanup_ce_jpegenc (jpegenc);
}

GST_END_TEST;


GST_START_TEST (test_ce_jpegenc_different_caps)
{
  GstElement *jpegenc;
  GstBuffer *buffer;
  GstCaps *caps;
  GstCaps *allowed_caps;

  /* now use a more restricted one and check the resulting caps */
  jpegenc = setup_ce_jpegenc (&any_sinktemplate);
  gst_element_set_state (jpegenc, GST_STATE_PLAYING);

  /* push first buffer with 2560x1700 resolution */
  caps = gst_caps_new_simple ("video/x-raw", "width", G_TYPE_INT,
      2560, "height", G_TYPE_INT, 1700, "framerate",
      GST_TYPE_FRACTION, 1, 1, "format", G_TYPE_STRING, "NV12", NULL);
  fail_unless (gst_pad_set_caps (mysrcpad, caps));
  fail_unless ((buffer = create_cmem_buffer (2560 * 1700 * 3 / 2)) != NULL);
  gst_caps_unref (caps);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);

  /* check the allowed caps to see if a second buffer with a different
   * caps could be negotiated */
  allowed_caps = gst_pad_get_allowed_caps (mysrcpad);

  /* the caps we want to negotiate to */
  caps = gst_caps_new_simple ("video/x-raw", "width", G_TYPE_INT,
      480, "height", G_TYPE_INT, 320, "framerate",
      GST_TYPE_FRACTION, 1, 1, "format", G_TYPE_STRING, "UYVY", NULL);
  fail_unless (gst_caps_can_intersect (allowed_caps, caps));
  fail_unless (gst_pad_set_caps (mysrcpad, caps));

  /* push fourth buffer with 480x320 resolution */
  buffer = create_cmem_buffer (480 * 320 * 2);
  gst_caps_unref (caps);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);

  gst_caps_unref (allowed_caps);
  gst_element_set_state (jpegenc, GST_STATE_NULL);
  cleanup_ce_jpegenc (jpegenc);
}

GST_END_TEST;

GST_START_TEST (test_ce_jpegenc_properties)
{
  GstElement *jpegenc;
  GstBuffer *buffer;
  GstCaps *caps;
  gint res_qValue, res_rotation;

  jpegenc = setup_ce_jpegenc (&any_sinktemplate);
  gst_element_set_state (jpegenc, GST_STATE_PLAYING);

  caps = gst_caps_new_simple ("video/x-raw", "width", G_TYPE_INT,
      2560, "height", G_TYPE_INT, 1700, "framerate",
      GST_TYPE_FRACTION, 1, 1, "format", G_TYPE_STRING, "NV12", NULL);

  g_object_set (jpegenc, "qValue", 65, NULL);
  g_object_set (jpegenc, "rotation", 270, NULL);

  g_object_get (jpegenc,
      "qValue", &res_qValue, "rotation", &res_rotation, NULL);

  fail_unless (res_qValue == 65);
  fail_unless (res_rotation == 270);

  fail_unless (gst_pad_set_caps (mysrcpad, caps));
  fail_unless ((buffer = create_cmem_buffer (2560 * 1700 * 3 / 2)) != NULL);
  gst_caps_unref (caps);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);

  gst_element_set_state (jpegenc, GST_STATE_NULL);
  cleanup_ce_jpegenc (jpegenc);
}

GST_END_TEST;

/**
 * Also review:
 * ARM consume vs quality, for a given size
 * Time between each consecutive frame
 * Number of images encoded without errors
 */

static Suite *
ce_jpegenc_suite (void)
{
  Suite *s = suite_create ("ce_jpegenc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_ce_jpegenc_getcaps);
  tcase_add_test (tc_chain, test_ce_jpegenc_different_caps);
  tcase_add_test (tc_chain, test_ce_jpegenc_properties);

  return s;
}

GST_CHECK_MAIN (ce_jpegenc);
