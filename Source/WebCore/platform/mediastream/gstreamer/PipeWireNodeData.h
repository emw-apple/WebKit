/*
 * Copyright (C) 2025 Igalia S.L.
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
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)

#include "GStreamerCommon.h"

namespace WebCore {

struct PipeWireNodeData {
    WTF_DEPRECATED_MAKE_STRUCT_FAST_ALLOCATED(PipeWireNodeData);

    PipeWireNodeData(uint32_t objectId)
        : objectId(objectId)
        , persistentId(emptyString())
        , fd(-1)
        , path(emptyString())
        , label(emptyString())
    {
    }

    uint32_t objectId;
    String persistentId;
    int fd;
    GRefPtr<GstCaps> caps;
    String path;
    String label;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
