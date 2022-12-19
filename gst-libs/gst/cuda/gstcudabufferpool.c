/* GStreamer
 * Copyright (C) <2018-2019> Seungha Yang <seungha.yang@navercorp.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstcudabufferpool.h"
#include "gstcudacontext.h"
#include "gstcudamemory.h"

GST_DEBUG_CATEGORY_STATIC (gst_cuda_buffer_pool_debug);
#define GST_CAT_DEFAULT gst_cuda_buffer_pool_debug

struct _GstCudaBufferPoolPrivate
{
  GstVideoInfo info;
};

#define gst_cuda_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstCudaBufferPool, gst_cuda_buffer_pool,
    GST_TYPE_BUFFER_POOL);

static const gchar **
gst_cuda_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META, NULL
  };

  return options;
}

static gboolean
gst_cuda_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstCudaBufferPool *self = GST_CUDA_BUFFER_POOL (pool);
  GstCudaBufferPoolPrivate *priv = self->priv;
  GstCaps *caps = NULL;
  guint size, min_buffers, max_buffers;
  GstVideoInfo info;
  GstMemory *mem;
  GstCudaMemory *cmem;

  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
          &max_buffers)) {
    GST_WARNING_OBJECT (self, "invalid config");
    return FALSE;
  }

  if (!caps) {
    GST_WARNING_OBJECT (pool, "no caps in config");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (self, "Failed to convert caps to video-info");
    return FALSE;
  }

  mem = gst_cuda_allocator_alloc (NULL, self->context, NULL, &info);
  if (!mem) {
    GST_WARNING_OBJECT (self, "Failed to allocate memory");
    return FALSE;
  }

  cmem = GST_CUDA_MEMORY_CAST (mem);

  gst_buffer_pool_config_set_params (config, caps,
      GST_VIDEO_INFO_SIZE (&cmem->info), min_buffers, max_buffers);

  priv->info = info;

  gst_memory_unref (mem);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);
}

static GstFlowReturn
gst_cuda_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstCudaBufferPool *self = GST_CUDA_BUFFER_POOL_CAST (pool);
  GstCudaBufferPoolPrivate *priv = self->priv;
  GstVideoInfo *info = &priv->info;
  GstBuffer *buf;
  GstMemory *mem;
  GstCudaMemory *cmem;

  mem = gst_cuda_allocator_alloc (NULL, self->context, NULL, &priv->info);
  if (!mem) {
    GST_WARNING_OBJECT (pool, "Cannot create CUDA memory");
    return GST_FLOW_ERROR;
  }

  cmem = GST_CUDA_MEMORY_CAST (mem);

  buf = gst_buffer_new ();

  gst_buffer_append_memory (buf, mem);

  GST_DEBUG_OBJECT (pool, "adding GstVideoMeta");
  gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
      GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
      cmem->info.offset, cmem->info.stride);

  *buffer = buf;

  return GST_FLOW_OK;
}

/**
 * gst_cuda_buffer_pool_new:
 * @context: The #GstCudaContext to use for the new buffer pool
 *
 * Returns: A newly created #GstCudaBufferPool
 *
 * Since: 1.22
 */
GstBufferPool *
gst_cuda_buffer_pool_new (GstCudaContext * context)
{
  GstCudaBufferPool *self;

  g_return_val_if_fail (GST_IS_CUDA_CONTEXT (context), NULL);

  self = g_object_new (GST_TYPE_CUDA_BUFFER_POOL, NULL);
  gst_object_ref_sink (self);

  self->context = gst_object_ref (context);

  return GST_BUFFER_POOL_CAST (self);
}

static void
gst_cuda_buffer_pool_dispose (GObject * object)
{
  GstCudaBufferPool *self = GST_CUDA_BUFFER_POOL_CAST (object);

  gst_clear_object (&self->context);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_cuda_buffer_pool_class_init (GstCudaBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *bufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->dispose = gst_cuda_buffer_pool_dispose;

  bufferpool_class->get_options = gst_cuda_buffer_pool_get_options;
  bufferpool_class->set_config = gst_cuda_buffer_pool_set_config;
  bufferpool_class->alloc_buffer = gst_cuda_buffer_pool_alloc;

  GST_DEBUG_CATEGORY_INIT (gst_cuda_buffer_pool_debug, "cudabufferpool", 0,
      "CUDA Buffer Pool");
}

static void
gst_cuda_buffer_pool_init (GstCudaBufferPool * pool)
{
  pool->priv = gst_cuda_buffer_pool_get_instance_private (pool);
}
