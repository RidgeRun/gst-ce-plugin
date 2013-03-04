/*
 * gstceh264enc.c
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
#include <ti/sdo/codecs/h264enc/ih264venc.h>

#include "gstceh264enc.h"
#include "gstcevidenc.h"


GstStaticCaps gst_ce_h264enc_sink_caps = GST_STATIC_CAPS ("video/x-raw, "
    "   format = (string) NV12,"
    "   framerate=(fraction)[ 0, 120], "
    "   width=(int)[ 128, 4080 ], " "   height=(int)[ 96, 4096 ]");

GstStaticCaps gst_ce_h264enc_src_caps = GST_STATIC_CAPS ("video/x-h264, "
    "   framerate=(fraction)[ 0, 120], "
    "   width=(int)[ 128, 4080 ], " "   height=(int)[ 96, 4096 ]");

static void
gst_ce_h264enc_setup (GObject * object)
{
  GstCEVidEnc *cevidenc = (GstCEVidEnc *) (object);

  IH264VENC_Params *h264_params;
  IH264VENC_DynamicParams *h264_dyn_params;

  /* Alloc the params and set a default value */
  h264_params = g_malloc0 (sizeof (IH264VENC_Params));
  *h264_params = IH264VENC_PARAMS;

  h264_dyn_params = g_malloc0 (sizeof (IH264VENC_DynamicParams));
  *h264_dyn_params = H264VENC_TI_IH264VENC_DYNAMICPARAMS;

  /* Add the extends params to the original params */
  cevidenc->codec_params->size = sizeof (IH264VENC_Params);
  cevidenc->codec_dyn_params->size = sizeof (IH264VENC_DynamicParams);

  if (cevidenc->codec_params != NULL) {
    GST_DEBUG ("Codec params not NULL, copy and free them\n");
    h264_params->videncParams = *cevidenc->codec_params;
    g_free (cevidenc->codec_params);
  }
  cevidenc->codec_params = (VIDENC1_Params *) h264_params;

  if (cevidenc->codec_dyn_params != NULL) {
    GST_DEBUG ("Codec dynamic params not NULL, copy and free them\n");
    h264_dyn_params->videncDynamicParams = *cevidenc->codec_dyn_params;
    g_free (cevidenc->codec_dyn_params);
  }
  cevidenc->codec_dyn_params = (VIDENC1_DynamicParams *) h264_dyn_params;

}

GstCECodecData gst_ce_h264enc = {
  .name = "h264enc",
  .long_name = "H.264",
  .src_caps = &gst_ce_h264enc_src_caps,
  .sink_caps = &gst_ce_h264enc_sink_caps,
  .setup = NULL,
  .install_properties = NULL,
  .set_property = NULL,
  .get_property = NULL,
};
