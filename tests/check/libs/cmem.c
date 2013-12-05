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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>
#include <ext/cmem/gstcmemallocator.h>
#include <gst/gst.h>


GST_START_TEST (test_cmem_allocator)
{
  GstAllocator *alloc;
  GstMemory *mem;
  GstAllocationParams params;
  GstMapInfo info;

  /* memory using the cmem API */
  gst_cmem_init ();

  alloc = gst_allocator_find ("ContiguousMemory");
  fail_unless (alloc != NULL);

  gst_allocation_params_init (&params);
  mem = gst_allocator_alloc (alloc, 1024, &params);

  gst_memory_map (mem, &info, GST_MAP_READ);
  gst_memory_unmap (mem, &info);

  gst_memory_unref (mem);
  gst_object_unref (alloc);

  return;
}

GST_END_TEST;

static Suite *
cmem_suite (void)
{
  Suite *s = suite_create ("CMEM support libray");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_cmem_allocator);

  return s;
}

GST_CHECK_MAIN (cmem);
