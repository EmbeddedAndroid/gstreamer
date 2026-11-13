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

#ifndef __GST_V4L2_DMA_HEAP_POOL_H__
#define __GST_V4L2_DMA_HEAP_POOL_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_V4L2_DMA_HEAP_POOL (gst_v4l2_dma_heap_pool_get_type ())
G_DECLARE_FINAL_TYPE (GstV4l2DmaHeapPool, gst_v4l2_dma_heap_pool,
    GST, V4L2_DMA_HEAP_POOL, GstBufferPool)

GstBufferPool *gst_v4l2_dma_heap_pool_new (const gchar * heap,
    const GstVideoInfo * info);

G_END_DECLS

#endif /* __GST_V4L2_DMA_HEAP_POOL_H__ */
