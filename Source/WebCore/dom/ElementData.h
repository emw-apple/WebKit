/*
 * Copyright (C) 2013, 2014 Apple Inc. All rights reserved.
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

#include "Attribute.h"
#include "SpaceSplitString.h"
#include <wtf/IndexedRange.h>
#include <wtf/StdLibExtras.h>
#include <wtf/TypeCasts.h>
#include <wtf/Vector.h>

namespace WebCore {

class Attr;
class ImmutableStyleProperties;
class ShareableElementData;
class StyleProperties;
class UniqueElementData;

DECLARE_ALLOCATOR_WITH_HEAP_IDENTIFIER(ElementData);
class ElementData : public RefCounted<ElementData> {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED_WITH_HEAP_IDENTIFIER(ElementData, ElementData);
public:
    // Override RefCounted's deref() to ensure operator delete is called on
    // the appropriate subclass type.
    void deref();

    static const unsigned attributeNotFound = static_cast<unsigned>(-1);

    void setClassNames(SpaceSplitString&& classNames) const { m_classNames = WTFMove(classNames); }
    const SpaceSplitString& classNames() const { return m_classNames; }
    static constexpr ptrdiff_t classNamesMemoryOffset() { return OBJECT_OFFSETOF(ElementData, m_classNames); }

    const AtomString& idForStyleResolution() const { return m_idForStyleResolution; }
    static constexpr ptrdiff_t idForStyleResolutionMemoryOffset() { return OBJECT_OFFSETOF(ElementData, m_idForStyleResolution); }
    void setIdForStyleResolution(const AtomString& newId) const { m_idForStyleResolution = newId; }

    const StyleProperties* inlineStyle() const { return m_inlineStyle.get(); }
    const ImmutableStyleProperties* presentationalHintStyle() const;

    unsigned length() const;
    bool isEmpty() const { return !length(); }

    std::span<const Attribute> attributes() const LIFETIME_BOUND;
    const Attribute& attributeAt(unsigned index) const;
    const Attribute* findAttributeByName(const QualifiedName&) const;
    unsigned findAttributeIndexByName(const QualifiedName&) const;
    unsigned findAttributeIndexByName(const AtomString& name, bool shouldIgnoreAttributeCase) const;

    bool hasID() const { return !m_idForStyleResolution.isNull(); }
    bool hasClass() const { return !m_classNames.isEmpty(); }
    bool hasName() const { return m_arraySizeAndFlags & s_flagHasNameAttribute; }

    bool isEquivalent(const ElementData* other) const;

    bool isUnique() const { return m_arraySizeAndFlags & s_flagIsUnique; }
    static uint32_t isUniqueFlag() { return s_flagIsUnique; }

    static constexpr ptrdiff_t arraySizeAndFlagsMemoryOffset() { return OBJECT_OFFSETOF(ElementData, m_arraySizeAndFlags); }
    static inline uint32_t styleAttributeIsDirtyFlag() { return s_flagStyleAttributeIsDirty; }
    static uint32_t animatedSVGAttributesAreDirtyFlag() { return s_flagAnimatedSVGAttributesAreDirty; }

    static uint32_t arraySizeOffset() { return s_flagCount; }

private:
    mutable uint32_t m_arraySizeAndFlags;

    static const uint32_t s_arraySize = 27;
    static const uint32_t s_flagCount = 5;
    static const uint32_t s_flagIsUnique = 1;
    static const uint32_t s_flagHasNameAttribute = 1 << 1;
    static const uint32_t s_flagPresentationalHintStyleIsDirty = 1 << 2;
    static const uint32_t s_flagStyleAttributeIsDirty = 1 << 3;
    static const uint32_t s_flagAnimatedSVGAttributesAreDirty = 1 << 4;
    static const uint32_t s_flagsMask = (1 << s_flagCount) - 1;
    // FIXME: could the SVG specific flags go to some SVG class?

    inline void updateFlag(uint32_t flag, bool set) const
    {
        if (set)
            m_arraySizeAndFlags |= flag;
        else
            m_arraySizeAndFlags &= ~flag;
    }
    static inline uint32_t arraySizeAndFlagsFromOther(const ElementData& other, bool isUnique);

protected:
    ElementData();
    explicit ElementData(unsigned arraySize);
    ElementData(const ElementData&, bool isUnique);

    unsigned arraySize() const { return m_arraySizeAndFlags >> s_flagCount; }

    void setHasNameAttribute(bool hasName) const { updateFlag(s_flagHasNameAttribute, hasName); }

    bool styleAttributeIsDirty() const { return m_arraySizeAndFlags & s_flagStyleAttributeIsDirty; }
    void setStyleAttributeIsDirty(bool isDirty) const { updateFlag(s_flagStyleAttributeIsDirty, isDirty); }

    bool presentationalHintStyleIsDirty() const { return m_arraySizeAndFlags & s_flagPresentationalHintStyleIsDirty; }
    void setPresentationalHintStyleIsDirty(bool isDirty) const { updateFlag(s_flagPresentationalHintStyleIsDirty, isDirty); }

    bool animatedSVGAttributesAreDirty() const { return m_arraySizeAndFlags & s_flagAnimatedSVGAttributesAreDirty; }
    void setAnimatedSVGAttributesAreDirty(bool dirty) const { updateFlag(s_flagAnimatedSVGAttributesAreDirty, dirty); }

    mutable RefPtr<StyleProperties> m_inlineStyle;
    mutable SpaceSplitString m_classNames;
    mutable AtomString m_idForStyleResolution;

private:
    friend class Element;
    friend class StyledElement;
    friend class ShareableElementData;
    friend class UniqueElementData;
    friend class SVGElement;
    friend class HTMLImageElement;

    void destroy();

    const Attribute* attributeBase() const;
    std::span<const Attribute> attributeSpan() const { return unsafeMakeSpan(attributeBase(), length()); }
    const Attribute* findAttributeByName(const AtomString& name, bool shouldIgnoreAttributeCase) const;

    Ref<UniqueElementData> makeUniqueCopy() const;
};

DECLARE_ALLOCATOR_WITH_HEAP_IDENTIFIER(ShareableElementData);
class ShareableElementData : public ElementData {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED_WITH_HEAP_IDENTIFIER(ShareableElementData, ShareableElementData);
public:
    static Ref<ShareableElementData> createWithAttributes(std::span<const Attribute>);

    explicit ShareableElementData(std::span<const Attribute>);
    explicit ShareableElementData(const UniqueElementData&);
    ~ShareableElementData();

    static constexpr ptrdiff_t attributeArrayMemoryOffset() { return OBJECT_OFFSETOF(ShareableElementData, m_attributeArray); }

    std::span<Attribute> attributes() { return unsafeMakeSpan(m_attributeArray, arraySize()); }
    std::span<const Attribute> attributes() const { return unsafeMakeSpan(m_attributeArray, arraySize()); }

    Attribute m_attributeArray[0];
};

class UniqueElementData : public ElementData {
public:
    static Ref<UniqueElementData> create();
    Ref<ShareableElementData> makeShareableCopy() const;

    // These functions do no error/duplicate checking.
    void addAttribute(const QualifiedName&, const AtomString&);
    void removeAttributeAt(unsigned index);

    Attribute& attributeAt(unsigned index);
    Attribute* findAttributeByName(const QualifiedName&);

    std::span<const Attribute> attributes() const LIFETIME_BOUND { return m_attributeVector.span(); }

    UniqueElementData();
    explicit UniqueElementData(const ShareableElementData&);
    explicit UniqueElementData(const UniqueElementData&);

    static constexpr ptrdiff_t attributeVectorMemoryOffset() { return OBJECT_OFFSETOF(UniqueElementData, m_attributeVector); }

    mutable RefPtr<ImmutableStyleProperties> m_presentationalHintStyle;
    typedef Vector<Attribute, 4> AttributeVector;
    AttributeVector m_attributeVector;
};

inline void ElementData::deref()
{
    if (!derefBase())
        return;
    destroy();
}

inline unsigned ElementData::length() const
{
    if (auto* uniqueData = dynamicDowncast<UniqueElementData>(*this))
        return uniqueData->m_attributeVector.size();
    return arraySize();
}

inline const Attribute* ElementData::attributeBase() const
{
    if (auto* uniqueData = dynamicDowncast<UniqueElementData>(*this))
        return uniqueData->m_attributeVector.span().data();
    return uncheckedDowncast<ShareableElementData>(*this).m_attributeArray;
}

inline const ImmutableStyleProperties* ElementData::presentationalHintStyle() const
{
    if (auto* uniqueData = dynamicDowncast<UniqueElementData>(*this))
        return uniqueData->m_presentationalHintStyle.get();
    return nullptr;
}

inline std::span<const Attribute> ElementData::attributes() const
{
    if (isUnique())
        return uncheckedDowncast<UniqueElementData>(*this).attributes();
    return uncheckedDowncast<ShareableElementData>(*this).attributes();
}

ALWAYS_INLINE const Attribute* ElementData::findAttributeByName(const AtomString& name, bool shouldIgnoreAttributeCase) const
{
    unsigned index = findAttributeIndexByName(name, shouldIgnoreAttributeCase);
    if (index != attributeNotFound)
        return &attributeAt(index);
    return nullptr;
}

ALWAYS_INLINE unsigned ElementData::findAttributeIndexByName(const QualifiedName& name) const
{
    auto attributes = attributeSpan();
    for (auto [i, attribute] : indexedRange(attributes)) {
        if (attribute.name().matches(name))
            return i;
    }
    return attributeNotFound;
}

// We use a boolean parameter instead of calling shouldIgnoreAttributeCase so that the caller
// can tune the behavior (hasAttribute is case sensitive whereas getAttribute is not).
ALWAYS_INLINE unsigned ElementData::findAttributeIndexByName(const AtomString& name, bool shouldIgnoreAttributeCase) const
{
    auto attributes = attributeSpan();
    if (attributes.empty())
        return attributeNotFound;

    auto& caseAdjustedName = shouldIgnoreAttributeCase ? name.convertToASCIILowercase() : name;

    for (auto [i, attribute] : indexedRange(attributes)) {
        if (!attribute.name().hasPrefix()) {
            if (attribute.localName() == caseAdjustedName)
                return i;
        } else {
            if (attribute.name().toString() == caseAdjustedName)
                return i;
        }
    }

    return attributeNotFound;
}

ALWAYS_INLINE const Attribute* ElementData::findAttributeByName(const QualifiedName& name) const
{
    for (auto& attribute : attributeSpan()) {
        if (attribute.name().matches(name))
            return &attribute;
    }
    return nullptr;
}

inline const Attribute& ElementData::attributeAt(unsigned index) const
{
    return attributeSpan()[index];
}

inline void UniqueElementData::addAttribute(const QualifiedName& attributeName, const AtomString& value)
{
    m_attributeVector.append(Attribute(attributeName, value));
}

inline void UniqueElementData::removeAttributeAt(unsigned index)
{
    m_attributeVector.removeAt(index);
}

inline Attribute& UniqueElementData::attributeAt(unsigned index)
{
    return m_attributeVector.at(index);
}

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::ShareableElementData)
    static bool isType(const WebCore::ElementData& elementData) { return !elementData.isUnique(); }
SPECIALIZE_TYPE_TRAITS_END()

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::UniqueElementData)
    static bool isType(const WebCore::ElementData& elementData) { return elementData.isUnique(); }
SPECIALIZE_TYPE_TRAITS_END()
