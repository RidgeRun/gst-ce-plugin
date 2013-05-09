/*
 * gstcejpegenc.h
 *
 * Copyright (C) 2013 RidgeRun, LLC (http://www.ridgerun.com)
 * Author: Carlos Gomez Viquez <carlos.gomez@ridgerun.com>
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

#ifndef __GST_CE_JPEGENC_H__
#define __GST_CE_JPEGENC_H__

#include "gstceimgenc.h"

G_BEGIN_DECLS
#define GST_TYPE_CE_JPEGENC \
  (gst_ce_jpegenc_get_type())
#define GST_CE_JPEGENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CE_JPEGENC,GstCeJpegEnc))
#define GST_CE_JPEGENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CE_JPEGENC,GstCeJpegEncClass))
#define GST_IS_CE_JPEGENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CE_JPEGENC))
#define GST_IS_CE_JPEGENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CE_JPEGENC))
typedef struct _GstCeJpegEnc GstCeJpegEnc;
typedef struct _GstCeJpegEncClass GstCeJpegEncClass;

struct _GstCeJpegEnc
{
  GstCeImgEnc encoder;
};

struct _GstCeJpegEncClass
{
  GstCeImgEncClass parent_class;
};

GType gst_ce_jpegenc_get_type (void);

G_END_DECLS
#endif /* __GST_CE_JPEGENC_H__ */
