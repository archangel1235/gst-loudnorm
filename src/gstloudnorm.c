/* GStreamer
 * Copyright (C) 2024 Cradlewise <sivaram@cradlewise.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstloudnorm
 *
 * The loudnorm element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v fakesrc ! loudnorm ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/audio/gstaudiofilter.h>
#include "gstloudnorm.h"
#include <math.h> 

GST_DEBUG_CATEGORY_STATIC (gst_loudnorm_debug_category);
#define GST_CAT_DEFAULT gst_loudnorm_debug_category

/* prototypes */


static void gst_loudnorm_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_loudnorm_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_loudnorm_dispose (GObject * object);
static void gst_loudnorm_finalize (GObject * object);

static gboolean gst_loudnorm_setup (GstAudioFilter * filter,
    const GstAudioInfo * info);
static GstFlowReturn gst_loudnorm_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static GstFlowReturn gst_loudnorm_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);

enum
{
  PROP_0,
  PROP_TARGET_LOUDNESS
};

/* pad templates */


static GstStaticPadTemplate gst_loudnorm_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
    "audio/x-raw, "
    "format = (string) S16LE, "
    "channels = (int) 1, "
    "rate = (int) 48000"
    )
  );

static GstStaticPadTemplate gst_loudnorm_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
    "audio/x-raw, "
    "format = (string) S16LE, "
    "channels = (int) 1, "
    "rate = (int) 48000"
    )
  );

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstLoudnorm, gst_loudnorm, GST_TYPE_AUDIO_FILTER,
    GST_DEBUG_CATEGORY_INIT (gst_loudnorm_debug_category, "loudnorm", 0,
        "debug category for loudnorm element"));

static void
gst_loudnorm_class_init (GstLoudnormClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);
  GstAudioFilterClass *audio_filter_class = GST_AUDIO_FILTER_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_loudnorm_src_template);
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_loudnorm_sink_template);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Loundness Normalization", "Generic", "Loundness Normalization using ebur128",
      "Sivaram <sivaram@cradlewise.com>");

  gobject_class->set_property = gst_loudnorm_set_property;
  gobject_class->get_property = gst_loudnorm_get_property;

  g_object_class_install_property (gobject_class, PROP_TARGET_LOUDNESS,
      g_param_spec_float ("target-loudness", "Target Loudness",
          "Target Loudness in LUFS", -40.0, 0.0, -27.0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gobject_class->dispose = gst_loudnorm_dispose;
  gobject_class->finalize = gst_loudnorm_finalize;
  audio_filter_class->setup = GST_DEBUG_FUNCPTR (gst_loudnorm_setup);
  //base_transform_class->transform = GST_DEBUG_FUNCPTR (gst_loudnorm_transform);
  base_transform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_loudnorm_transform_ip);

}

static void
gst_loudnorm_init (GstLoudnorm * this)
{
  this->ebur128_state = ebur128_init (1, 48000, EBUR128_MODE_I);
  this->target_loudness = -23.0;
}

void
gst_loudnorm_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstLoudnorm *this = GST_LOUDNORM (object);

  GST_DEBUG_OBJECT (this, "set_property");

  switch (property_id) {
    case PROP_TARGET_LOUDNESS:
      this->target_loudness = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_loudnorm_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstLoudnorm *this = GST_LOUDNORM (object);

  GST_DEBUG_OBJECT (this, "get_property");

  switch (property_id) {
    case PROP_TARGET_LOUDNESS:
      g_value_set_float (value, this->target_loudness);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_loudnorm_dispose (GObject * object)
{
  GstLoudnorm *this = GST_LOUDNORM (object);

  GST_DEBUG_OBJECT (this, "dispose");

  if (this->ebur128_state) {
    ebur128_destroy (&this->ebur128_state);
  }

  G_OBJECT_CLASS (gst_loudnorm_parent_class)->dispose (object);
}

void
gst_loudnorm_finalize (GObject * object)
{
  GstLoudnorm *loudnorm = GST_LOUDNORM (object);

  GST_DEBUG_OBJECT (loudnorm, "finalize");

  /* clean up object here */

  G_OBJECT_CLASS (gst_loudnorm_parent_class)->finalize (object);
}

static gboolean
gst_loudnorm_setup (GstAudioFilter * filter, const GstAudioInfo * info)
{
  GstLoudnorm *this = GST_LOUDNORM (filter);

  GST_DEBUG_OBJECT (this, "setup");

  return TRUE;
}

/* transform */
static GstFlowReturn
gst_loudnorm_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstLoudnorm *loudnorm = GST_LOUDNORM (trans);

  GST_DEBUG_OBJECT (loudnorm, "transform");

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_loudnorm_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstLoudnorm *this = GST_LOUDNORM (trans);

  GST_DEBUG_OBJECT (this, "transform_ip");

  GstMapInfo map;
  if (!gst_buffer_map (buf, &map, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (this, "Failed to map buffer");
    return GST_FLOW_ERROR;
  }

  guint samples = gst_buffer_get_size (buf) / sizeof (int16_t);

  int16_t *samples_ptr = (int16_t *) map.data;

  ebur128_add_frames_short (this->ebur128_state, samples_ptr, samples);

  double loudness;
  ebur128_loudness_global (this->ebur128_state, &loudness);

  double gain = this->target_loudness - loudness;

  for (int i = 0; i < samples; ++i) {
    if (samples_ptr[i] * pow(10, gain / 20.0) > 32767) samples_ptr[i] = 32767;
    else if (samples_ptr[i] * pow(10, gain / 20.0) < -32768) samples_ptr[i] = -32768;
    else samples_ptr[i] = (short)(samples_ptr[i] * pow(10, gain / 20.0));
  }

  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "loudnorm", GST_RANK_NONE,
      GST_TYPE_LOUDNORM);
}


#ifndef VERSION
#define VERSION "0.0.1"
#endif
#ifndef PACKAGE
#define PACKAGE "Audio Filters"
#endif
#ifndef PACKAGE_NAME
#define PACKAGE_NAME "Loudness Normalization"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "Cradlewise, Inc."
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    loudnorm,
    "Loudness Normalization using ebur128",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
