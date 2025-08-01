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

#include "AccessibilityRenderObject.h"

namespace WebCore {
    
class AccessibilityList final : public AccessibilityRenderObject {
public:
    static Ref<AccessibilityList> create(AXID, RenderObject&, AXObjectCache&);
    static Ref<AccessibilityList> create(AXID, Node&, AXObjectCache&);
    virtual ~AccessibilityList();

private:
    explicit AccessibilityList(AXID, RenderObject&, AXObjectCache&);
    explicit AccessibilityList(AXID, Node&, AXObjectCache&);
    bool isListInstance() const final { return true; }

    bool isUnorderedList() const final;
    bool isOrderedList() const final;
    bool isDescriptionList() const final;

    bool computeIsIgnored() const final;
    AccessibilityRole determineAccessibilityRole() final;
    bool childHasPseudoVisibleListItemMarkers(const Node*);
    void updateRoleAfterChildrenCreation() final { updateRole(); }
    AccessibilityRole determineAccessibilityRoleWithCleanChildren();
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::AccessibilityList) \
    static bool isType(const WebCore::AccessibilityObject& object) { return object.isListInstance(); } \
SPECIALIZE_TYPE_TRAITS_END()
