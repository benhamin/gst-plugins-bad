/* GStreamer
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

/**
 * SECTION:element-mfvp9enc
 * @title: mfvp9enc
 *
 * This element encodes raw video into VP9 compressed data.
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 -v videotestsrc ! mfvp9enc ! matroskamux ! filesink location=videotestsrc.mkv
 * ]| This example pipeline will encode a test video source to VP9 using
 * Media Foundation encoder, and muxes it in a mkv container.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstmfvideoenc.h"
#include "gstmfvp9enc.h"
#include <wrl.h>

using namespace Microsoft::WRL;

GST_DEBUG_CATEGORY (gst_mf_vp9_enc_debug);
#define GST_CAT_DEFAULT gst_mf_vp9_enc_debug

enum
{
  GST_MF_VP9_ENC_RC_MODE_CBR = 0,
  GST_MF_VP9_ENC_RC_MODE_QUALITY,
};

#define GST_TYPE_MF_VP9_ENC_RC_MODE (gst_mf_vp9_enc_rc_mode_get_type())
static GType
gst_mf_vp9_enc_rc_mode_get_type (void)
{
  static GType rc_mode_type = 0;

  static const GEnumValue rc_mode_types[] = {
    {GST_MF_VP9_ENC_RC_MODE_CBR, "Constant bitrate", "cbr"},
    {GST_MF_VP9_ENC_RC_MODE_QUALITY, "Quality-based variable bitrate", "qvbr"},
    {0, NULL, NULL}
  };

  if (!rc_mode_type) {
    rc_mode_type = g_enum_register_static ("GstMFVP9EncRCMode", rc_mode_types);
  }
  return rc_mode_type;
}

enum
{
  GST_MF_VP9_ENC_CONTENT_TYPE_UNKNOWN,
  GST_MF_VP9_ENC_CONTENT_TYPE_FIXED_CAMERA_ANGLE,
};

#define GST_TYPE_MF_VP9_ENC_CONTENT_TYPE (gst_mf_vp9_enc_content_type_get_type())
static GType
gst_mf_vp9_enc_content_type_get_type (void)
{
  static GType content_type = 0;

  static const GEnumValue content_types[] = {
    {GST_MF_VP9_ENC_CONTENT_TYPE_UNKNOWN, "Unknown", "unknown"},
    {GST_MF_VP9_ENC_CONTENT_TYPE_FIXED_CAMERA_ANGLE,
        "Fixed Camera Angle, such as a webcam", "fixed"},
    {0, NULL, NULL}
  };

  if (!content_type) {
    content_type =
        g_enum_register_static ("GstMFVP9EncContentType", content_types);
  }
  return content_type;
}

enum
{
  PROP_0,
  PROP_BITRATE,
  PROP_RC_MODE,
  PROP_MAX_BITRATE,
  PROP_QUALITY_VS_SPEED,
  PROP_GOP_SIZE,
  PROP_THREADS,
  PROP_CONTENT_TYPE,
  PROP_LOW_LATENCY,
};

#define DEFAULT_BITRATE (2 * 1024)
#define DEFAULT_RC_MODE GST_MF_VP9_ENC_RC_MODE_CBR
#define DEFAULT_MAX_BITRATE 0
#define DEFAULT_QUALITY_VS_SPEED 50
#define DEFAULT_GOP_SIZE -1
#define DEFAULT_THREADS 0
#define DEFAULT_CONTENT_TYPE GST_MF_VP9_ENC_CONTENT_TYPE_UNKNOWN
#define DEFAULT_LOW_LATENCY FALSE

typedef struct _GstMFVP9Enc
{
  GstMFVideoEnc parent;

  /* properteies */
  guint bitrate;

  /* device dependent properties */
  guint rc_mode;
  guint max_bitrate;
  guint quality_vs_speed;
  guint gop_size;
  guint threads;
  guint content_type;
  gboolean low_latency;
} GstMFVP9Enc;

typedef struct _GstMFVP9EncClass
{
  GstMFVideoEncClass parent_class;
} GstMFVP9EncClass;

static GstElementClass *parent_class = NULL;

static void gst_mf_vp9_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_mf_vp9_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static gboolean gst_mf_vp9_enc_set_option (GstMFVideoEnc * mfenc,
    GstVideoCodecState * state, IMFMediaType * output_type);
static gboolean gst_mf_vp9_enc_set_src_caps (GstMFVideoEnc * mfenc,
    GstVideoCodecState * state, IMFMediaType * output_type);

static void
gst_mf_vp9_enc_class_init (GstMFVP9EncClass * klass, gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstMFVideoEncClass *mfenc_class = GST_MF_VIDEO_ENC_CLASS (klass);
  GstMFVideoEncClassData *cdata = (GstMFVideoEncClassData *) data;
  GstMFVideoEncDeviceCaps *device_caps = &cdata->device_caps;
  gchar *long_name;
  gchar *classification;

  parent_class = (GstElementClass *) g_type_class_peek_parent (klass);

  gobject_class->get_property = gst_mf_vp9_enc_get_property;
  gobject_class->set_property = gst_mf_vp9_enc_set_property;

  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate", "Bitrate in kbit/sec", 1,
          (G_MAXUINT >> 10), DEFAULT_BITRATE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  if (device_caps->rc_mode) {
    g_object_class_install_property (gobject_class, PROP_RC_MODE,
        g_param_spec_enum ("rc-mode", "Rate Control Mode",
            "Rate Control Mode",
            GST_TYPE_MF_VP9_ENC_RC_MODE, DEFAULT_RC_MODE,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    /* NOTE: documentation will be done by only for default device */
    if (cdata->is_default) {
      gst_type_mark_as_plugin_api (GST_TYPE_MF_VP9_ENC_RC_MODE,
          (GstPluginAPIFlags) 0);
    }
  }

  if (device_caps->max_bitrate) {
    g_object_class_install_property (gobject_class, PROP_MAX_BITRATE,
        g_param_spec_uint ("max-bitrate", "Max Bitrate",
            "The maximum bitrate applied when rc-mode is \"pcvbr\" in kbit/sec "
            "(0 = MFT default)", 0, (G_MAXUINT >> 10),
            DEFAULT_MAX_BITRATE,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->quality_vs_speed) {
    g_object_class_install_property (gobject_class, PROP_QUALITY_VS_SPEED,
        g_param_spec_uint ("quality-vs-speed", "Quality Vs Speed",
            "Quality and speed tradeoff, [0, 33]: Low complexity, "
            "[34, 66]: Medium complexity, [67, 100]: High complexity", 0, 100,
            DEFAULT_QUALITY_VS_SPEED,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->gop_size) {
    g_object_class_install_property (gobject_class, PROP_GOP_SIZE,
        g_param_spec_int ("gop-size", "GOP size",
            "The number of pictures from one GOP header to the next. "
            "Depending on GPU vendor implementation, zero gop-size might "
            "produce only one keyframe at the beginning (-1 for automatic)",
            -1, G_MAXINT, DEFAULT_GOP_SIZE,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->threads) {
    g_object_class_install_property (gobject_class, PROP_THREADS,
        g_param_spec_uint ("threads", "Threads",
            "The number of worker threads used by a encoder, "
            "(0 = MFT default)", 0, 16,
            DEFAULT_THREADS,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  if (device_caps->content_type) {
    g_object_class_install_property (gobject_class, PROP_CONTENT_TYPE,
        g_param_spec_enum ("content-type", "Content Type",
            "Indicates the type of video content",
            GST_TYPE_MF_VP9_ENC_CONTENT_TYPE, DEFAULT_CONTENT_TYPE,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    /* NOTE: documentation will be done by only for default device */
    if (cdata->is_default) {
      gst_type_mark_as_plugin_api (GST_TYPE_MF_VP9_ENC_CONTENT_TYPE,
          (GstPluginAPIFlags) 0);
    }
  }

  if (device_caps->low_latency) {
    g_object_class_install_property (gobject_class, PROP_LOW_LATENCY,
        g_param_spec_boolean ("low-latency", "Low Latency",
            "Enable low latency encoding",
            DEFAULT_LOW_LATENCY,
            (GParamFlags) (GST_PARAM_CONDITIONALLY_AVAILABLE |
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  }

  long_name = g_strdup_printf ("Media Foundation %s", cdata->device_name);
  classification = g_strdup_printf ("Codec/Encoder/Video%s",
      (cdata->enum_flags & MFT_ENUM_FLAG_HARDWARE) == MFT_ENUM_FLAG_HARDWARE ?
          "/Hardware" : "");
  gst_element_class_set_metadata (element_class, long_name,
      classification,
      "Microsoft Media Foundation VP9 Encoder",
      "Seungha Yang <seungha@centricular.com>");
  g_free (long_name);
  g_free (classification);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  mfenc_class->set_option = GST_DEBUG_FUNCPTR (gst_mf_vp9_enc_set_option);
  mfenc_class->set_src_caps = GST_DEBUG_FUNCPTR (gst_mf_vp9_enc_set_src_caps);

  mfenc_class->codec_id = MFVideoFormat_VP90;
  mfenc_class->enum_flags = cdata->enum_flags;
  mfenc_class->device_index = cdata->device_index;
  mfenc_class->device_caps = *device_caps;

  g_free (cdata->device_name);
  gst_caps_unref (cdata->sink_caps);
  gst_caps_unref (cdata->src_caps);
  g_free (cdata);
}

static void
gst_mf_vp9_enc_init (GstMFVP9Enc * self)
{
  self->bitrate = DEFAULT_BITRATE;
  self->rc_mode = DEFAULT_RC_MODE;
  self->max_bitrate = DEFAULT_MAX_BITRATE;
  self->quality_vs_speed = DEFAULT_QUALITY_VS_SPEED;
  self->gop_size = DEFAULT_GOP_SIZE;
  self->threads = DEFAULT_THREADS;
  self->content_type = DEFAULT_CONTENT_TYPE;
  self->low_latency = DEFAULT_LOW_LATENCY;
}

static void
gst_mf_vp9_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMFVP9Enc *self = (GstMFVP9Enc *) (object);

  switch (prop_id) {
    case PROP_BITRATE:
      g_value_set_uint (value, self->bitrate);
      break;
    case PROP_RC_MODE:
      g_value_set_enum (value, self->rc_mode);
      break;
    case PROP_MAX_BITRATE:
      g_value_set_uint (value, self->max_bitrate);
      break;
    case PROP_QUALITY_VS_SPEED:
      g_value_set_uint (value, self->quality_vs_speed);
      break;
    case PROP_GOP_SIZE:
      g_value_set_int (value, self->gop_size);
      break;
    case PROP_THREADS:
      g_value_set_uint (value, self->threads);
      break;
    case PROP_CONTENT_TYPE:
      g_value_set_enum (value, self->content_type);
      break;
    case PROP_LOW_LATENCY:
      g_value_set_boolean (value, self->low_latency);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mf_vp9_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMFVP9Enc *self = (GstMFVP9Enc *) (object);

  switch (prop_id) {
    case PROP_BITRATE:
      self->bitrate = g_value_get_uint (value);
      break;
    case PROP_RC_MODE:
      self->rc_mode = g_value_get_enum (value);
      break;
    case PROP_MAX_BITRATE:
      self->max_bitrate = g_value_get_uint (value);
      break;
    case PROP_QUALITY_VS_SPEED:
      self->quality_vs_speed = g_value_get_uint (value);
      break;
    case PROP_GOP_SIZE:
      self->gop_size = g_value_get_int (value);
      break;
    case PROP_THREADS:
      self->threads = g_value_get_uint (value);
      break;
    case PROP_CONTENT_TYPE:
      self->content_type = g_value_get_enum (value);
      break;
    case PROP_LOW_LATENCY:
      self->low_latency = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static guint
gst_mf_vp9_enc_rc_mode_to_enum (guint rc_mode)
{
  switch (rc_mode) {
    case GST_MF_VP9_ENC_RC_MODE_CBR:
      return eAVEncCommonRateControlMode_CBR;
    case GST_MF_VP9_ENC_RC_MODE_QUALITY:
      return eAVEncCommonRateControlMode_Quality;
    default:
      return G_MAXUINT;
  }
}

static guint
gst_mf_vp9_enc_content_type_to_enum (guint rc_mode)
{
  switch (rc_mode) {
    case GST_MF_VP9_ENC_CONTENT_TYPE_UNKNOWN:
      return eAVEncVideoContentType_Unknown;
    case GST_MF_VP9_ENC_CONTENT_TYPE_FIXED_CAMERA_ANGLE:
      return eAVEncVideoContentType_FixedCameraAngle;
    default:
      return G_MAXUINT;
  }
}

#define WARNING_HR(hr,func) \
  G_STMT_START { \
    if (!gst_mf_result (hr)) { \
      GST_WARNING_OBJECT (self, G_STRINGIFY(func) " failed, hr: 0x%x", (guint) hr); \
    } \
  } G_STMT_END

static gboolean
gst_mf_vp9_enc_set_option (GstMFVideoEnc * mfenc, GstVideoCodecState * state,
    IMFMediaType * output_type)
{
  GstMFVP9Enc *self = (GstMFVP9Enc *) mfenc;
  GstMFVideoEncClass *klass = GST_MF_VIDEO_ENC_GET_CLASS (mfenc);
  GstMFVideoEncDeviceCaps *device_caps = &klass->device_caps;
  HRESULT hr;
  GstMFTransform *transform = mfenc->transform;

  hr = output_type->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_VP90);
  if (!gst_mf_result (hr))
    return FALSE;

  if (!gst_mf_result (hr))
    return FALSE;

  hr = output_type->SetUINT32 (MF_MT_AVG_BITRATE,
      MIN (self->bitrate * 1024, G_MAXUINT - 1));
  if (!gst_mf_result (hr))
    return FALSE;

  if (device_caps->rc_mode) {
    guint rc_mode;
    rc_mode = gst_mf_vp9_enc_rc_mode_to_enum (self->rc_mode);
    if (rc_mode != G_MAXUINT) {
      hr = gst_mf_transform_set_codec_api_uint32 (transform,
          &CODECAPI_AVEncCommonRateControlMode, rc_mode);
      WARNING_HR (hr, CODECAPI_AVEncCommonRateControlMode);
    }
  }

  if (device_caps->max_bitrate && self->max_bitrate > 0) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncCommonMaxBitRate,
        MIN (self->max_bitrate * 1024, G_MAXUINT - 1));
    WARNING_HR (hr, CODECAPI_AVEncCommonMaxBitRate);
  }

  if (device_caps->quality_vs_speed) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncCommonQualityVsSpeed,
        self->quality_vs_speed);
    WARNING_HR (hr, CODECAPI_AVEncCommonQualityVsSpeed);
  }

  if (device_caps->gop_size) {
    GstVideoInfo *info = &state->info;
    gint gop_size = self->gop_size;
    gint fps_n, fps_d;

    /* Set default value (10 sec or 250 frames) like that of x264enc */
    if (gop_size < 0) {
      fps_n = GST_VIDEO_INFO_FPS_N (info);
      fps_d = GST_VIDEO_INFO_FPS_D (info);
      if (fps_n <= 0 || fps_d <= 0) {
        gop_size = 250;
      } else {
        gop_size = 10 * fps_n / fps_d;
      }

      GST_DEBUG_OBJECT (self, "Update GOP size to %d", gop_size);
    }

    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncMPVGOPSize, gop_size);
    WARNING_HR (hr, CODECAPI_AVEncMPVGOPSize);
  }

  if (device_caps->threads) {
    hr = gst_mf_transform_set_codec_api_uint32 (transform,
        &CODECAPI_AVEncNumWorkerThreads, self->threads);
    WARNING_HR (hr, CODECAPI_AVEncNumWorkerThreads);
  }

  if (device_caps->content_type) {
    guint content_type;
    content_type = gst_mf_vp9_enc_content_type_to_enum (self->content_type);
    if (content_type != G_MAXUINT) {
      hr = gst_mf_transform_set_codec_api_uint32 (transform,
          &CODECAPI_AVEncVideoContentType, content_type);
      WARNING_HR (hr, CODECAPI_AVEncVideoContentType);
    }
  }

  if (device_caps->low_latency) {
    hr = gst_mf_transform_set_codec_api_boolean (transform,
        &CODECAPI_AVLowLatencyMode, self->low_latency);
    WARNING_HR (hr, CODECAPI_AVLowLatencyMode);
  }

  return TRUE;
}

static gboolean
gst_mf_vp9_enc_set_src_caps (GstMFVideoEnc * mfenc,
    GstVideoCodecState * state, IMFMediaType * output_type)
{
  GstMFVP9Enc *self = (GstMFVP9Enc *) mfenc;
  GstVideoCodecState *out_state;
  GstStructure *s;
  GstCaps *out_caps;
  GstTagList *tags;

  out_caps = gst_caps_new_empty_simple ("video/x-vp9");
  s = gst_caps_get_structure (out_caps, 0);

  out_state = gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (self),
      out_caps, state);

  GST_INFO_OBJECT (self, "output caps: %" GST_PTR_FORMAT, out_state->caps);

  /* encoder will keep it around for us */
  gst_video_codec_state_unref (out_state);

  tags = gst_tag_list_new_empty ();
  gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_ENCODER,
      gst_element_get_metadata (GST_ELEMENT_CAST (self),
          GST_ELEMENT_METADATA_LONGNAME), NULL);
  gst_video_encoder_merge_tags (GST_VIDEO_ENCODER (self), tags,
      GST_TAG_MERGE_REPLACE);
  gst_tag_list_unref (tags);

  return TRUE;
}

void
gst_mf_vp9_enc_plugin_init (GstPlugin * plugin, guint rank)
{
  GTypeInfo type_info = {
    sizeof (GstMFVP9EncClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_mf_vp9_enc_class_init,
    NULL,
    NULL,
    sizeof (GstMFVP9Enc),
    0,
    (GInstanceInitFunc) gst_mf_vp9_enc_init,
  };
  GUID subtype = MFVideoFormat_VP90;

  GST_DEBUG_CATEGORY_INIT (gst_mf_vp9_enc_debug, "mfvp9enc", 0, "mfvp9enc");

  gst_mf_video_enc_register (plugin, rank, &subtype, &type_info);
}