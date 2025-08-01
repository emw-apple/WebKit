/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#pragma once

#include "AccessibilityNodeObject.h"
#include "AccessibilityObject.h"

namespace WebCore {

class HTMLOptionElement;

class AccessibilityMenuListOption final : public AccessibilityNodeObject {
public:
    static Ref<AccessibilityMenuListOption> create(AXID, HTMLOptionElement&, AXObjectCache&);
    void setParent(AccessibilityObject* parent) { m_parent = parent; }

private:
    explicit AccessibilityMenuListOption(AXID, HTMLOptionElement&, AXObjectCache&);

    bool isMenuListOption() const final { return true; }

    AccessibilityRole determineAccessibilityRole() final { return AccessibilityRole::MenuListOption; }
    bool canHaveChildren() const final { return false; }
    AccessibilityObject* parentObject() const final { return m_parent.get(); }

    HTMLOptionElement* optionElement() const;
    Element* actionElement() const final;
    bool isEnabled() const final;
    bool isVisible() const final;
    bool isOffScreen() const final;
    bool isSelected() const final;
    void setSelected(bool) final;
    bool canSetSelectedAttribute() const final;
    LayoutRect elementRect() const final;
    String stringValue() const final;
    bool computeIsIgnored() const final;

    WeakPtr<AccessibilityObject> m_parent;
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::AccessibilityMenuListOption) \
    static bool isType(const WebCore::AccessibilityObject& object) { return object.isMenuListOption(); } \
SPECIALIZE_TYPE_TRAITS_END()
