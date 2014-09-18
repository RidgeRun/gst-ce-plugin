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
/**
 * SECTION:element-priority
 *
 * This file declares the "priority" element, which modifies the priorities
 * and scheduling of the gstreamer threads.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sched.h>
#include <gst/gst.h>

#include "gstpriority.h"

/* pad templates */
static GstStaticPadTemplate gst_priority_src_template = 
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_priority_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

    
GST_DEBUG_CATEGORY_STATIC (gst_priority_debug);
#define GST_CAT_DEFAULT gst_priority_debug

enum
{
  ARG_0,
  ARG_NICE,
  ARG_SCHEDULER,
  ARG_RTPRIORITY,
};

/* class initialization */
#define gst_priority_parent_class parent_class
G_DEFINE_TYPE (GstPriority, gst_priority, GST_TYPE_BASE_TRANSFORM);

/* prototypes */
static void gst_priority_set_property (GObject * object, guint property_id, 
    const GValue * value, GParamSpec * pspec);
static void gst_priority_get_property (GObject * object, guint property_id, 
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_priority_transform_ip (GstBaseTransform * trans, 
    GstBuffer * buf);
static gboolean gst_priority_start (GstBaseTransform * trans);

static void
gst_priority_class_init (GstPriorityClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_priority_set_property;
  gobject_class->get_property = gst_priority_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      ARG_NICE,
      g_param_spec_int ("nice",
          "nice",
          "Nice value for the thread. Only valid for scheduler OTHER",
          -20, 19, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      ARG_SCHEDULER,
      g_param_spec_int ("scheduler",
          "scheduler",
          "Scheduler to use: 0 - OTHER, 1 - RT FIFO, 2 - RT RoundRobin",
          0, 2, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      ARG_RTPRIORITY,
      g_param_spec_int ("rtpriority",
          "rtpriority",
          "Real time priority: 1 (lower), 99 (higher). "
          "Only valid for scheduler RT FIFO or RT RR ",
          1, 99, 1, G_PARAM_READWRITE));
          
  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_priority_start);
  base_transform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_priority_transform_ip);
  
  gst_element_class_set_static_metadata (element_class,
    "Priority adjuster",
    "Misc",
    "This element can change the priorites of segments of a pipeline",
    "Diego Dompe; RidgeRun");
      
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_priority_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_priority_sink_template));
}

static void
gst_priority_init (GstPriority * priority)
{
  priority->nice = 0;
  priority->nice_changed = 0;
  priority->scheduler = 0;
  priority->rtpriority = 0;
  priority->rt_changed = FALSE;
  priority->rtmin = 0;
  priority->rtmax = 0;
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM_CAST (priority), TRUE);
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM_CAST (priority), TRUE);
}

void
gst_priority_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPriority *priority = GST_PRIORITY (object);

  switch (prop_id) {
    case ARG_NICE:{
      gint a = g_value_get_int (value) + 20;
      gint b = getpriority (PRIO_PROCESS, 0) + 20;
      priority->nice_changed = a - b;
      GST_WARNING_OBJECT(priority,"Nice values: a %d, b %d, nice change %d",a,b,priority->nice_changed);
      break;
    }
    case ARG_SCHEDULER:{
      priority->scheduler = g_value_get_int (value);
      priority->rt_changed = TRUE;
      break;
    }
    case ARG_RTPRIORITY:{
      priority->rtpriority = g_value_get_int (value);
      priority->rt_changed = TRUE;
      break;
    }

    default:{
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }
}

void
gst_priority_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPriority *priority = GST_PRIORITY (object);

  switch (prop_id) {
    case ARG_NICE:{
      g_value_set_int (value, priority->nice);
      break;
    }
    case ARG_SCHEDULER:{
      g_value_set_int (value, priority->scheduler);
      break;
    }
    case ARG_RTPRIORITY:{
      g_value_set_int (value, priority->rtpriority);
      break;
    }

    default:{
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }
}

static gboolean
gst_priority_start (GstBaseTransform * trans)
{
  GstPriority *priority = GST_PRIORITY (trans);
  struct sched_param param;

  priority->nice = getpriority (PRIO_PROCESS, 0);
  priority->rtmin = sched_get_priority_min (SCHED_FIFO);
  priority->rtmax = sched_get_priority_max (SCHED_FIFO);
  if (!priority->scheduler) {
	priority->scheduler = sched_getscheduler (0);
  }
  if (priority->scheduler != SCHED_OTHER && !priority->rtpriority) {
    sched_getparam (0, &param);
    priority->rtpriority = param.sched_priority;
  }

  if (priority->rtmax < priority->rtmin) {
    GST_ELEMENT_WARNING (priority, RESOURCE, FAILED, (NULL),
        ("Your kernel rt max priority is less than your min priority"));
  }
  GST_INFO ("RT priorities: min %d, max %d", priority->rtmin, priority->rtmax);

  return TRUE;
}

static GstFlowReturn
gst_priority_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  gint newnice;
  GstPriority *priority = GST_PRIORITY (trans);

  if (priority->nice_changed) {
    errno = 0;
    newnice = nice (priority->nice_changed);
    if ((newnice == -1) && (errno)) {
      GST_ELEMENT_WARNING (priority, RESOURCE, FAILED, (NULL),
          ("Failed to set the request nice level, errno %d", errno));
    } else {
      GST_INFO ("%s: nice value is now %d",
          gst_element_get_name (priority), newnice);
    }
    priority->nice_changed = 0;
    priority->nice = newnice;
  }

  if (priority->rt_changed) {
    int policy;
    struct sched_param param;

    switch (priority->scheduler) {
      case 2:
        policy = SCHED_RR;
        break;
      case 1:
        policy = SCHED_FIFO;
        break;
      case 0:
      default:
        policy = SCHED_OTHER;
        break;
    }
    param.sched_priority = priority->rtpriority;
    if (sched_setscheduler (0, policy, &param) == -1) {
      GST_ELEMENT_WARNING (priority, RESOURCE, FAILED, (NULL),
          ("Failed to set the request rt scheduler (%d) or priority (%d),"
              " errno %d", priority->scheduler,priority->rtpriority,errno));
    }
    priority->rt_changed = FALSE;
  }
  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * plugin)
{

  GST_DEBUG_CATEGORY_INIT (gst_priority_debug, "priority", 0,
    "Debug category for priority element");
      
  return gst_element_register (plugin, "priority", GST_RANK_NONE,
      GST_TYPE_PRIORITY);
}

#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "http://www.ridgerun.com"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    priority,
    "Set pipeline fragment thread priority",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)

