/*
 * gstcemp3enc.h
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

#ifndef __GST_CE_MP3ENC_H__
#define __GST_CE_MP3ENC_H__

#include "gstceaudenc.h"

G_BEGIN_DECLS
#define GST_TYPE_CE_MP3ENC \
  (gst_ce_mp3enc_get_type())
#define GST_CE_MP3ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CE_MP3ENC,GstCeMp3Enc))
#define GST_CE_MP3ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CE_MP3ENC,GstCeMp3EncClass))
#define GST_IS_CE_MP3ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CE_MP3ENC))
#define GST_IS_CE_MP3ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CE_MP3ENC))
typedef struct _GstCeMp3Enc GstCeMp3Enc;
typedef struct _GstCeMp3EncClass GstCeMp3EncClass;

struct _GstCeMp3Enc
{
  GstCeAudEnc encoder;
  /* Audio info */
  gint rate;
  gint profile;
  gint channels;

};

struct _GstCeMp3EncClass
{
  GstCeAudEncClass parent_class;
};

GType gst_ce_mp3enc_get_type (void);

G_END_DECLS
#endif /* __GST_CE_MP3ENC_H__ */
