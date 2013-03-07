/*
 * gstceutils.h
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

#ifndef __GST_CE_UTILS_H__
#define __GST_CE_UTILS_H__

#include <gst/gst.h>

GST_DEBUG_CATEGORY_EXTERN (ce_debug);
#define GST_CAT_DEFAULT ce_debug

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)));

G_BEGIN_DECLS

typedef struct _GstCECodecData GstCECodecData;

struct _GstCECodecData
{
  /* Name of the codec implementation */
  const gchar *name;
  /* Descriptive name for the codec */
  const gchar *long_name;
  /* Capabilities of the codec's input*/
  GstStaticCaps *src_caps;
  /* Capabilities of the codec's output*/
  GstStaticCaps *sink_caps;
  /* Fuction to alloc and Initialize resources*/
  void (*setup)(GObject *);
  /* Function to define element src caps*/
  gboolean (*set_src_caps)(GObject *, GstCaps *);
  /* Functions to provide custom properties */
  void (*install_properties)(GObjectClass *);
  void (*set_property) (GObject *,guint,const GValue *,GParamSpec *);
  void (*get_property)(GObject *,guint,GValue *, GParamSpec *);  
};
 
G_END_DECLS
#endif /*__GST_CE_UTILS_H__*/
