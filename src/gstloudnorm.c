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
 * The loudnorm element does loundness normalization on single channel.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 filesrc location=audio_3.wav ! wavparse ! audioconvert ! \
 *  audioresample ! 'audio/x-raw,format=S16LE,channels=1,rate=48000'! \
 *  loudnorm target-loudness=-23.0 ! audioconvert ! audioresample ! \
 *  autoaudiosink
 * ]|
 * this pipeline will normalize the loudness of the audio_3.wav file to -23.0 LUFS
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
static GstFlowReturn gst_loudnorm_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);

static void precomputeGaussianKernel(double* kernel);
double gaussianFilter(Queue* queue, double* kernel);
static void initQueue(Queue *queue);
static float topQueue(Queue *queue);
static void pushWithGaussianFilter(Queue* queue, double item, double* kernel); 

enum
{
  PROP_0,
  PROP_TARGET_LOUDNESS,
  PROP_TARGET_LRA,
  PROP_SILENT_THRESHOLD
};

/* Primitives to Gaussian Filter */
static void precomputeGaussianKernel(double* kernel) {
    double filterSum = 0.0;

    // Calculate Gaussian kernel
    for (int i = 0; i < FILTER_SIZE; i++) {
        double x = i - (FILTER_SIZE - 1) / 2.0;
        kernel[i] = exp(-0.5 * x / (FILTER_SIGMA * FILTER_SIGMA));
        filterSum += kernel[i];
    }

    // Normalize the kernel
    for (int i = 0; i < FILTER_SIZE; i++) {
        kernel[i] /= filterSum;
    }
}

double gaussianFilter(Queue* queue, double* kernel) {
    double sum = 0.0;
    int rear = queue->rear;
    int size = queue->size;

    // Apply Gaussian filter
    for (int i = 0; i < FILTER_SIZE && i < size; i++) {
        int index = (rear - i + QUEUE_SIZE) % QUEUE_SIZE;
        sum += queue->data[index] * kernel[i];
    }

    return sum;
}

/* Primitives for queue */

static void initQueue(Queue *queue) {
    queue->front = queue->rear = -1;
    queue->size = 0;
    // Initialize the queue with 0
    for (int i = 0; i < QUEUE_SIZE; i++) {
        queue->data[i] = 0.0;
    }
}

static float topQueue(Queue *queue) {
    return queue->data[queue->rear];
}

int isFull(Queue* queue) {
    return queue->size == QUEUE_SIZE;
}

void enqueue(Queue* queue, double item) {
    if (isFull(queue)) {
        queue->front = (queue->front + 1) % QUEUE_SIZE;
    } else {
        queue->size++;
    }
    queue->rear = (queue->rear + 1) % QUEUE_SIZE;
    queue->data[queue->rear] = item;
}

static void pushWithGaussianFilter(Queue* queue, double item, double* kernel) {
    enqueue(queue, item);
    if (queue->size < FILTER_SIZE) return;
    double filteredItem = gaussianFilter(queue, kernel);
    queue->data[queue->rear] = filteredItem;
}

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

  g_object_class_install_property (gobject_class, PROP_TARGET_LRA,
      g_param_spec_float ("target-lra", "Target LRA",
          "Target Loudness Range in LUFS", 1.0, 20.0, 7.0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SILENT_THRESHOLD,
      g_param_spec_float ("silent-threshold", "Silent Threshold",
          "Silent Threshold in LUFS", -80.0, 0.0, -50.0,
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
  this->ebur128_state = ebur128_init (1, 48000, EBUR128_MODE_I|EBUR128_MODE_LRA);
  this->target_loudness = -23.0;
  this->target_lra = 5.0;
  initQueue(&this->gain_history);
  precomputeGaussianKernel(this->kernel);
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
    case PROP_TARGET_LRA:
      this->target_lra = g_value_get_float (value);
      break;
    case PROP_SILENT_THRESHOLD:
      this->silence_threshold = g_value_get_float (value);
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
    case PROP_TARGET_LRA:
      g_value_set_float (value, this->target_lra);
      break;
    case PROP_SILENT_THRESHOLD:
      g_value_set_float (value, this->silence_threshold);
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
  GstLoudnorm *this = GST_LOUDNORM (object);

  GST_DEBUG_OBJECT (this, "finalize");

  if (this->ebur128_state) {
    ebur128_destroy (&this->ebur128_state);
  }

  G_OBJECT_CLASS (gst_loudnorm_parent_class)->finalize (object);
}

static gboolean
gst_loudnorm_setup (GstAudioFilter * filter, const GstAudioInfo * info)
{
  GstLoudnorm *this = GST_LOUDNORM (filter);

  GST_DEBUG_OBJECT (this, "setup");

  return TRUE;
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

  double loudness_shortterm;
  ebur128_loudness_shortterm (this->ebur128_state, &loudness_shortterm);

  double loudness_momentary;
  ebur128_loudness_momentary (this->ebur128_state, &loudness_momentary);

  if (loudness_shortterm == -HUGE_VAL) loudness_shortterm = -23.0;

  double shortterm_gain = this->target_loudness - loudness_shortterm;

  double momentary_gain = this->target_loudness - loudness_momentary;

  double gain = momentary_gain < shortterm_gain ? momentary_gain : shortterm_gain;  

  pushWithGaussianFilter(&this->gain_history, gain, this->kernel);

  gain = topQueue(&this->gain_history);
  
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
#define VERSION "0.1.0"
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
