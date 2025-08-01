/*
 * Copyright (C) 2016 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "AccessibilityLabel.h"

namespace WebCore {
    
using namespace HTMLNames;

AccessibilityLabel::AccessibilityLabel(AXID axID, RenderObject& renderer, AXObjectCache& cache)
    : AccessibilityRenderObject(axID, renderer, cache)
{
}

AccessibilityLabel::~AccessibilityLabel() = default;

Ref<AccessibilityLabel> AccessibilityLabel::create(AXID axID, RenderObject& renderer, AXObjectCache& cache)
{
    return adoptRef(*new AccessibilityLabel(axID, renderer, cache));
}

String AccessibilityLabel::stringValue() const
{
    if (containsOnlyStaticText())
        return textUnderElement();
    return AccessibilityNodeObject::stringValue();
}

static bool childrenContainOnlyStaticText(const AccessibilityObject::AccessibilityChildrenVector& children)
{
    if (children.isEmpty())
        return false;
    for (const auto& child : children) {
        if (child->role() == AccessibilityRole::StaticText)
            continue;
        if (child->isGroup()) {
            if (!childrenContainOnlyStaticText(child->unignoredChildren()))
                return false;
        } else
            return false;
    }
    return true;
}

bool AccessibilityLabel::containsOnlyStaticText() const
{
    // m_containsOnlyStaticTextDirty is set (if necessary) by addChildren(), so update our children before checking the flag.
    const_cast<AccessibilityLabel*>(this)->updateChildrenIfNecessary();
    if (m_containsOnlyStaticTextDirty) {
        m_containsOnlyStaticTextDirty = false;
        m_containsOnlyStaticText = childrenContainOnlyStaticText(const_cast<AccessibilityLabel*>(this)->unignoredChildren());
    }
    return m_containsOnlyStaticText;
}

void AccessibilityLabel::addChildren()
{
    AccessibilityRenderObject::addChildren();
    m_containsOnlyStaticTextDirty = true;
}

void AccessibilityLabel::clearChildren()
{
    AccessibilityRenderObject::clearChildren();
    m_containsOnlyStaticText = false;
    m_containsOnlyStaticTextDirty = false;
}

} // namespace WebCore
