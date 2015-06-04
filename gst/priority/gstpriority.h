/* GStreamer
 * Copyright (C) 2013 RidgeRun, LLC (http://www.ridgerun.com)
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses>.
 */

#ifndef _GST_PRIORITY_H_
#define _GST_PRIORITY_H_

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_PRIORITY \
  (gst_priority_get_type())
#define GST_PRIORITY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PRIORITY,GstPriority))
#define GST_PRIORITY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PRIORITY,GstPriorityClass))
#define GST_IS_PRIORITY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PRIORITY))
#define GST_IS_PRIORITY_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PRIORITY))

typedef struct _GstPriority GstPriority;
typedef struct _GstPriorityClass GstPriorityClass;

struct _GstPriority
{
  GstBaseTransform parent;

  GstPad *sinkpad;
  GstPad *srcpad;

  gint nice;
  gint nice_changed;
  gint rtpriority;
  gint scheduler;
  gboolean rt_changed;
  gint rtmin;
  gint rtmax;
};

struct _GstPriorityClass
{
  GstBaseTransformClass parent_class;
};

GType gst_priority_get_type (void);

G_END_DECLS

#endif
