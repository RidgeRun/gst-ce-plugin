/* GStreamer
 * Copyright (C) 2013 RidgeRun, LLC (http://www.ridgerun.com)
 *
 * Test that the CE plugin is loadable
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

#include <gst/check/gstcheck.h>
#include <stdlib.h>

GST_START_TEST (test_ce_plugin)
{
  GstPlugin *plugin = gst_plugin_load_by_name ("ceplugin");

  fail_if (plugin == NULL, "Could not load CE plugin");

  gst_object_unref (plugin);

}

GST_END_TEST;

GST_START_TEST (test_ce_update_reg)
{
  GstElement *encoder;

  /* Ask for elements the first time */
  encoder = gst_element_factory_make ("ce_h264enc", "enc");
  GST_DEBUG ("Creating element ce_h264enc %p", encoder);
  fail_unless (encoder != NULL);

  gst_object_unref (encoder);

  GST_DEBUG ("calls gst_update_registry");
  gst_update_registry ();

  /* Ask for elements the second time */

  encoder = gst_element_factory_make ("ce_h264enc", "enc");
  GST_DEBUG ("Creating element ce_h264enc %p", encoder);
  fail_unless (encoder != NULL);

  gst_object_unref (encoder);
}

GST_END_TEST;

static Suite *
plugin_test_suite (void)
{
  Suite *s = suite_create ("Plugin");
  TCase *tc_chain = tcase_create ("existence");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_ce_plugin);
  tcase_add_test (tc_chain, test_ce_update_reg);

  return s;
}

GST_CHECK_MAIN (plugin_test);
