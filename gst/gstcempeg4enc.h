/*
 * gstcempeg4enc.h
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

#ifndef __GST_CE_MPEG4ENC_H__
#define __GST_CE_MPEG4ENC_H__

#include "gstcevidenc.h"

G_BEGIN_DECLS
#define GST_TYPE_CE_MPEG4ENC \
  (gst_ce_mpeg4enc_get_type())
#define GST_CE_MPEG4ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CE_MPEG4ENC,GstCeMpeg4Enc))
#define GST_CE_MPEG4ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CE_MPEG4ENC,GstCeMpeg4EncClass))
#define GST_IS_CE_MPEG4ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CE_MPEG4ENC))
#define GST_IS_CE_MPEG4ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CE_MPEG4ENC))
typedef struct _GstCeMpeg4Enc GstCeMpeg4Enc;
typedef struct _GstCeMpeg4EncClass GstCeMpeg4EncClass;

struct _GstCeMpeg4Enc
{
  GstCeVidEnc encoder;
  gint header_size;

};

struct _GstCeMpeg4EncClass
{
  GstCeVidEncClass parent_class;
};

GType gst_ce_mpeg4enc_get_type (void);

G_END_DECLS
#endif /* __GST_CE_MPEG4ENC_H__ */
