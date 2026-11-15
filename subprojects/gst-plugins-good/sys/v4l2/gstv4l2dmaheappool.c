/* GStreamer
 *
 * Copyright (C) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries.
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

/*
 * A buffer pool whose buffers are DMA-bufs allocated from a Linux DMA heap
 * (/dev/dma_heap/<name>). The v4l2 elements import them like buffers from a
 * downstream pool. The memory is never mapped here: a heap may hand out
 * memory the CPU cannot access, such as the TEE protected heaps.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#ifdef HAVE_LINUX_DMA_HEAP_H
#include <linux/dma-heap.h>
#endif

#include <gst/allocators/gstdmabuf.h>

#include "gstv4l2dmaheappool.h"

GST_DEBUG_CATEGORY_STATIC (v4l2dmaheappool_debug);
#define GST_CAT_DEFAULT v4l2dmaheappool_debug

struct _GstV4l2DmaHeapPool
{
  GstBufferPool parent;

  gint heap_fd;
  gchar *heap;
  GstAllocator *allocator;
  GstVideoInfo info;
  gsize size;
  gboolean add_video_meta;
};

G_DEFINE_TYPE_WITH_CODE (GstV4l2DmaHeapPool, gst_v4l2_dma_heap_pool,
    GST_TYPE_BUFFER_POOL, GST_DEBUG_CATEGORY_INIT (v4l2dmaheappool_debug,
        "v4l2dmaheappool", 0, "V4L2 DMA heap buffer pool"));

static const gchar **
gst_v4l2_dma_heap_pool_get_options (GstBufferPool * bpool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META, NULL };

  return options;
}

static gboolean
gst_v4l2_dma_heap_pool_set_config (GstBufferPool * bpool,
    GstStructure * config)
{
  GstV4l2DmaHeapPool *self = GST_V4L2_DMA_HEAP_POOL (bpool);
  guint size;

  if (!gst_buffer_pool_config_get_params (config, NULL, &size, NULL, NULL))
    return FALSE;

  self->size = size;
  self->add_video_meta = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META) &&
      GST_VIDEO_INFO_FORMAT (&self->info) != GST_VIDEO_FORMAT_UNKNOWN &&
      GST_VIDEO_INFO_FORMAT (&self->info) != GST_VIDEO_FORMAT_ENCODED;

  return GST_BUFFER_POOL_CLASS (gst_v4l2_dma_heap_pool_parent_class)->set_config
      (bpool, config);
}

static GstFlowReturn
gst_v4l2_dma_heap_pool_alloc_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
#ifdef HAVE_LINUX_DMA_HEAP_H
  GstV4l2DmaHeapPool *self = GST_V4L2_DMA_HEAP_POOL (bpool);
  struct dma_heap_allocation_data data = {
    .len = self->size,
    .fd_flags = O_RDWR | O_CLOEXEC,
  };
  GstMemory *mem;
  GstBuffer *buf;

  if (ioctl (self->heap_fd, DMA_HEAP_IOCTL_ALLOC, &data) < 0) {
    GST_ERROR_OBJECT (self, "allocating %" G_GSIZE_FORMAT " bytes from %s "
        "failed: %s", self->size, self->heap, g_strerror (errno));
    return GST_FLOW_ERROR;
  }

  mem = gst_dmabuf_allocator_alloc (self->allocator, data.fd, self->size);
  if (!mem) {
    close (data.fd);
    return GST_FLOW_ERROR;
  }

  buf = gst_buffer_new ();
  gst_buffer_append_memory (buf, mem);

  if (self->add_video_meta)
    gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (&self->info), GST_VIDEO_INFO_WIDTH (&self->info),
        GST_VIDEO_INFO_HEIGHT (&self->info),
        GST_VIDEO_INFO_N_PLANES (&self->info), self->info.offset,
        self->info.stride);

  *buffer = buf;

  return GST_FLOW_OK;
#else
  return GST_FLOW_NOT_SUPPORTED;
#endif
}

static void
gst_v4l2_dma_heap_pool_finalize (GObject * object)
{
  GstV4l2DmaHeapPool *self = GST_V4L2_DMA_HEAP_POOL (object);

  if (self->heap_fd >= 0)
    close (self->heap_fd);
  g_free (self->heap);
  gst_clear_object (&self->allocator);

  G_OBJECT_CLASS (gst_v4l2_dma_heap_pool_parent_class)->finalize (object);
}

static void
gst_v4l2_dma_heap_pool_class_init (GstV4l2DmaHeapPoolClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *pool_class = GST_BUFFER_POOL_CLASS (klass);

  object_class->finalize = gst_v4l2_dma_heap_pool_finalize;
  pool_class->get_options = gst_v4l2_dma_heap_pool_get_options;
  pool_class->set_config = gst_v4l2_dma_heap_pool_set_config;
  pool_class->alloc_buffer = gst_v4l2_dma_heap_pool_alloc_buffer;
}

static void
gst_v4l2_dma_heap_pool_init (GstV4l2DmaHeapPool * self)
{
  self->heap_fd = -1;
}

/**
 * gst_v4l2_dma_heap_pool_new:
 * @heap: a DMA heap name under /dev/dma_heap, or an absolute path
 * @info: (nullable): the layout of the frames, used for the video meta
 *
 * Returns: (transfer full) (nullable): a new pool, or %NULL with errno set
 * if the heap cannot be opened (ENOSYS: built without linux/dma-heap.h).
 */
GstBufferPool *
gst_v4l2_dma_heap_pool_new (const gchar * heap, const GstVideoInfo * info)
{
  GstV4l2DmaHeapPool *self;
  gchar *path;
  gint fd, err;

#ifndef HAVE_LINUX_DMA_HEAP_H
  errno = ENOSYS;
  return NULL;
#endif

  path = g_path_is_absolute (heap) ? g_strdup (heap) :
      g_build_filename ("/dev/dma_heap", heap, NULL);
  fd = open (path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    err = errno;
    GST_ERROR ("cannot open DMA heap %s: %s", path, g_strerror (err));
    g_free (path);
    errno = err;
    return NULL;
  }

  self = g_object_new (GST_TYPE_V4L2_DMA_HEAP_POOL, NULL);
  gst_object_ref_sink (self);
  self->heap_fd = fd;
  self->heap = path;
  self->allocator = gst_dmabuf_allocator_new ();
  if (info)
    self->info = *info;
  else
    gst_video_info_init (&self->info);

  return GST_BUFFER_POOL (self);
}
