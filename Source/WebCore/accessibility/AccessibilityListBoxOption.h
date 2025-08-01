/*
 * Copyright (C) 2008 Apple Inc. All rights reserved.
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

#pragma once

#include "AccessibilityNodeObject.h"

namespace WebCore {

class HTMLElement;
class HTMLSelectElement;

class AccessibilityListBoxOption final : public AccessibilityNodeObject {
public:
    static Ref<AccessibilityListBoxOption> create(AXID, HTMLElement&, AXObjectCache&);
    virtual ~AccessibilityListBoxOption();

    bool isSelected() const final;
    void setSelected(bool) final;

private:
    explicit AccessibilityListBoxOption(AXID, HTMLElement&, AXObjectCache&);

    AccessibilityRole determineAccessibilityRole() final { return AccessibilityRole::ListBoxOption; }
    bool isEnabled() const final;
    bool isSelectedOptionActive() const final;
    String stringValue() const final;
    Element* actionElement() const final;
    bool canSetSelectedAttribute() const final;

    LayoutRect elementRect() const final;
    AccessibilityObject* parentObject() const final;

    bool isAccessibilityListBoxOptionInstance() const final { return true; }
    void addChildren() final
    {
        m_childrenInitialized = true;
        m_childrenDirty = false;
        m_subtreeDirty = false;
    }
    bool canHaveChildren() const final { return false; }
    HTMLSelectElement* listBoxOptionParentNode() const;
    int listBoxOptionIndex() const;
    IntRect listBoxOptionRect() const;
    AccessibilityObject* listBoxOptionAccessibilityObject(HTMLElement*) const;
    bool computeIsIgnored() const final;
};

} // namespace WebCore 

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::AccessibilityListBoxOption) \
    static bool isType(const WebCore::AccessibilityObject& object) { return object.isAccessibilityListBoxOptionInstance(); } \
SPECIALIZE_TYPE_TRAITS_END()
