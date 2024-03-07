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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GST_LOUDNORM_H_
#define _GST_LOUDNORM_H_

#include <gst/audio/gstaudiofilter.h>
#include <ebur128.h>

G_BEGIN_DECLS

#define GST_TYPE_LOUDNORM   (gst_loudnorm_get_type())
#define GST_LOUDNORM(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_LOUDNORM,GstLoudnorm))
#define GST_LOUDNORM_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_LOUDNORM,GstLoudnormClass))
#define GST_IS_LOUDNORM(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_LOUDNORM))
#define GST_IS_LOUDNORM_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_LOUDNORM))
#define QUEUE_SIZE 20
#define FILTER_SIZE 20
#define FILTER_SIGMA 1.8

typedef struct _GstLoudnorm GstLoudnorm;
typedef struct _GstLoudnormClass GstLoudnormClass;

typedef struct {
    double data[QUEUE_SIZE];
    int front;
    int rear;
    int size;
} Queue;

struct _GstLoudnorm
{
  GstAudioFilter base_loudnorm;
  ebur128_state *ebur128_state;
  float target_loudness;
  float target_lra;
  Queue gain_history;
  double kernel[FILTER_SIZE];
};

struct _GstLoudnormClass
{
  GstAudioFilterClass base_loudnorm_class;
};

GType gst_loudnorm_get_type (void);

G_END_DECLS

#endif
