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

#include "gstce.h"

GST_DEBUG_CATEGORY (ce_debug);

static gboolean
gst_encoders_register (GstPlugin * plugin)
{
  GstCECodecData *codec;

  codec = g_malloc0 (sizeof (GstCECodecData));
  codec->name = "h264";
  codec->long_name = "H.264";

  return gst_cevidenc_register (plugin, codec);

}

/* Register of all the elements of the plugin */
static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_encoders_register (plugin);

  GST_DEBUG_CATEGORY_INIT (ce_debug, "ce", 0,
      "TI plugin for CodecEngine debugging");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    ceplugin,
    "GStreamer Plugin for codecs based on CodecEngine API for Texas Instruments SoC",
    plugin_init, VERSION, "LGPL", "gst-ce-plugin", "RidgeRun")
