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

#ifndef __GST_CEPLUGIN_H__
#define __GST_CEPLUGIN_H__

#include <gst/gst.h>
#include <config.h>
#include "gstceutils.h"

GST_DEBUG_CATEGORY_EXTERN (ce_debug);
#define GST_CAT_DEFAULT ce_debug

G_BEGIN_DECLS

extern gboolean 
    gst_cevidenc_register (GstPlugin * plugin, GstCECodecData * codec);

G_END_DECLS
#endif /* __GST_CEPLUGIN_H__ */
