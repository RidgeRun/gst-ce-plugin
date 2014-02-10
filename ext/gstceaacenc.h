/*
 * gstceaacenc.h
 *
 * Copyright (C) 2013 RidgeRun, LLC (http://www.ridgerun.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 2 of the License, or
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

#ifndef __GST_CE_AACENC_H__
#define __GST_CE_AACENC_H__

#include "gstceaudenc.h"

G_BEGIN_DECLS
#define GST_TYPE_CE_AACENC \
  (gst_ce_aacenc_get_type())
#define GST_CE_AACENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CE_AACENC,GstCeAacEnc))
#define GST_CE_AACENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CE_AACENC,GstCeAacEncClass))
#define GST_IS_CE_AACENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CE_AACENC))
#define GST_IS_CE_AACENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CE_AACENC))
typedef struct _GstCeAacEnc GstCeAacEnc;
typedef struct _GstCeAacEncClass GstCeAacEncClass;

struct _GstCeAacEnc
{
  GstCeAudEnc encoder;
  /* Audio info */
  gint rate;
  gint profile;
  gint channels;

};

struct _GstCeAacEncClass
{
  GstCeAudEncClass parent_class;
};

GType gst_ce_aacenc_get_type (void);

G_END_DECLS
#endif /* __GST_CE_AACENC_H__ */
