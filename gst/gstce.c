/*
 * gstce.c
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

#include <string.h>
#include <xdc/std.h>
#include <ti/sdo/ce/Engine.h>
#include <gst/cmem/gstcmemallocator.h>

#include "gstce.h"
#include "gstceh264enc.h"
#include "gstcejpegenc.h"
#include "gstceaacenc.h"

GST_DEBUG_CATEGORY (ce_debug);

typedef struct _CeElement
{
  const gchar *name;
  guint rank;
    GType (*get_type) (void);
} CeElement;

static CeElement gst_ce_element_list[] = {
  {"ce_h264enc", GST_RANK_PRIMARY, gst_ce_h264enc_get_type},
  {"ce_jpegenc", GST_RANK_PRIMARY, gst_ce_jpegenc_get_type},
  {"ce_aacenc", GST_RANK_PRIMARY, gst_ce_aacenc_get_type},
};

/* Register of all the elements of the plugin */
static gboolean
plugin_init (GstPlugin * plugin)
{
  CeElement *element;
  GType element_type;
  gint num_elements, i;

  GST_DEBUG_CATEGORY_INIT (ce_debug, "ce", 0, "TI plugin for CodecEngine");

  /* Initialize CMEM allocator
   * Inside this function the Codec Engine is initialized*/
  gst_cmem_init ();

  /* Register elements */
  num_elements = ARRAY_SIZE (gst_ce_element_list);
  for (i = 0; i < num_elements; i++) {
    element = &gst_ce_element_list[i];
    element_type = element->get_type ();
    if (!gst_element_register (plugin, element->name, element->rank,
            element_type)) {
      GST_ERROR ("failed to register codec %s", element->name);
      return FALSE;
    }
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    ceplugin,
    "GStreamer Plugin for codecs based on CodecEngine API for Texas Instruments SoC",
    plugin_init, VERSION, "LGPL", "gst-ce-plugin", "RidgeRun")
