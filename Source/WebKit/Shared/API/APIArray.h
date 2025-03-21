/*
 * Copyright (C) 2010, 2013 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "APIObject.h"
#include <wtf/Forward.h>
#include <wtf/IteratorAdaptors.h>
#include <wtf/IteratorRange.h>
#include <wtf/Vector.h>

namespace API {

class Array final : public ObjectImpl<Object::Type::Array> {
private:
    template <class T>
    struct IsTypePredicate {
        bool operator()(const RefPtr<Object>& object) const { return object->type() == T::APIType; }
    };

    template <class T>
    struct GetObjectTransform {
        const T* operator()(const RefPtr<Object>& object) const { return downcast<T>(object.get()); }
    };

    template <typename T>
    using ElementsOfTypeRange = WTF::IteratorRange<WTF::TransformIterator<GetObjectTransform<T>, WTF::FilterIterator<IsTypePredicate<T>, Vector<RefPtr<Object>>::const_iterator>>>;

public:
    static Ref<Array> create();
    static Ref<Array> createWithCapacity(size_t);
    static Ref<Array> create(Vector<RefPtr<Object>>&&);
    static Ref<Array> createStringArray(const Vector<WTF::String>&);
    static Ref<Array> createStringArray(const std::span<const WTF::String>);
    Vector<WTF::String> toStringVector();
    Ref<Array> copy();

    template<typename T>
    T* at(size_t i) const
    {
        return dynamicDowncast<T>(m_elements[i].get());
    }

    Object* at(size_t i) const { return m_elements[i].get(); }
    RefPtr<Object> protectedAt(size_t i) const { return m_elements[i]; }

    size_t size() const { return m_elements.size(); }

    const Vector<RefPtr<Object>>& elements() const { return m_elements; }
    Vector<RefPtr<Object>>& elements() { return m_elements; }

    template<typename T>
    ElementsOfTypeRange<T> elementsOfType() const
    {
        return WTF::makeIteratorRange(
            WTF::makeTransformIterator(GetObjectTransform<T>(), WTF::makeFilterIterator(IsTypePredicate<T>(), m_elements.begin(), m_elements.end())),
            WTF::makeTransformIterator(GetObjectTransform<T>(), WTF::makeFilterIterator(IsTypePredicate<T>(), m_elements.end(), m_elements.end()))
        );
    }

    template<typename MatchFunction>
    unsigned removeAllMatching(const MatchFunction& matchFunction)
    {
        return m_elements.removeAllMatching(matchFunction);
    }

    template<typename T, typename MatchFunction>
    unsigned removeAllOfTypeMatching(NOESCAPE const MatchFunction& matchFunction)
    {
        return m_elements.removeAllMatching([&] (const RefPtr<Object>& object) -> bool {
            if (object->type() != T::APIType)
                return false;
            return matchFunction(static_pointer_cast<T>(object));
        });
    }

    void append(RefPtr<Object>&& element) { m_elements.append(WTFMove(element)); }

private:
    explicit Array(Vector<RefPtr<Object>>&& elements)
        : m_elements(WTFMove(elements))
    {
    }

    Vector<RefPtr<Object>> m_elements;
};

} // namespace API

SPECIALIZE_TYPE_TRAITS_API_OBJECT(Array);
