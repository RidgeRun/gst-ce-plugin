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

GST_DEBUG_CATEGORY (ce_debug);

static GstCECodecData *gst_cevidenc_list[] = {
  &gst_ce_h264enc,
};

static gboolean
gst_encoders_register (GstPlugin * plugin)
{
  Engine_AlgInfo alg_info;
  Engine_Error status;
  gint num_algs = 0;
  gint num_encs = 0;
  gint i, j;
  gboolean codec_found = FALSE;
  gboolean ret = FALSE;

  /* Get all algorithms configured in the Codec Engine */
  status = Engine_getNumAlgs ((Char *) CODEC_ENGINE, &num_algs);
  if (status == Engine_EOK) {
    GST_DEBUG ("%s: number of algorithms = %d", CODEC_ENGINE, num_algs);
  } else {
    GST_ERROR ("failed to get the number of algorithms "
        "configured into %s: %d\n", CODEC_ENGINE, status);
    goto out;
  }

  alg_info.algInfoSize = sizeof (Engine_AlgInfo);

  for (i = 0; i < num_algs; i++) {

    status = Engine_getAlgInfo ((Char *) CODEC_ENGINE, &alg_info, i);
    if (status == Engine_EOK) {
      GST_DEBUG ("algorithm[%d] = %s", i, alg_info.name);
    } else {
      GST_ERROR ("failed to get %s algorithm[%d] information", CODEC_ENGINE, i);
      goto out;
    }

    codec_found = FALSE;
    num_encs = ARRAY_SIZE (gst_cevidenc_list);
    for (j = 0; j < num_encs && !codec_found; j++) {
      if (!strcmp (alg_info.name, gst_cevidenc_list[j]->name)) {
        GST_DEBUG ("found %s element data", gst_cevidenc_list[j]->name);
        ret = gst_cevidenc_register (plugin, gst_cevidenc_list[j]);
        if (!ret) {
          GST_ERROR ("failed to register codec %s", gst_cevidenc_list[j]->name);
          goto out;
        }
        codec_found = TRUE;
      }
    }

  }

out:
  return ret;
}

/* Register of all the elements of the plugin */
static gboolean
plugin_init (GstPlugin * plugin)
{

  GST_DEBUG_CATEGORY_INIT (ce_debug, "ce", 0,
      "TI plugin for CodecEngine debugging");

  /* Initialize CMEM allocator
   * Inside this function the Codec Engine is initialized*/
  gst_cmem_init ();

  /* Register encoders */
  gst_encoders_register (plugin);


  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    ceplugin,
    "GStreamer Plugin for codecs based on CodecEngine API for Texas Instruments SoC",
    plugin_init, VERSION, "LGPL", "gst-ce-plugin", "RidgeRun")
