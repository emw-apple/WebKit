/*
 * Copyright (C) 2007-2017 Apple Inc. All rights reserved.
 * Copyright (C) 2009 Google Inc. All rights reserved.
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

// FIXME: Could move what Vector and Deque share into a separate file.
// Deque doesn't actually use Vector.

#include <algorithm>
#include <iterator>
#include <wtf/Vector.h>

namespace WTF {

template<typename T, size_t inlineCapacity> class DequeIteratorBase;
template<typename T, size_t inlineCapacity> class DequeIterator;
template<typename T, size_t inlineCapacity> class DequeConstIterator;

template<typename T, size_t inlineCapacity> class Deque final {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(Deque);
public:
    typedef T ValueType;

    typedef DequeIterator<T, inlineCapacity> iterator;
    typedef DequeConstIterator<T, inlineCapacity> const_iterator;
    typedef std::reverse_iterator<iterator> reverse_iterator;
    typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

    Deque();
    Deque(std::initializer_list<T>);
    Deque(const Deque&);
    Deque(Deque&&);
    ~Deque();

    Deque& operator=(const Deque&);
    Deque& operator=(Deque&&);

    void swap(Deque&);

    size_t size() const { return m_start <= m_end ? m_end - m_start : m_end + m_buffer.capacity() - m_start; }
    bool isEmpty() const { return m_start == m_end; }

    iterator begin() LIFETIME_BOUND { return iterator(this, m_start); }
    iterator end() LIFETIME_BOUND { return iterator(this, m_end); }
    const_iterator begin() const LIFETIME_BOUND { return const_iterator(this, m_start); }
    const_iterator end() const LIFETIME_BOUND { return const_iterator(this, m_end); }
    reverse_iterator rbegin() LIFETIME_BOUND { return reverse_iterator(end()); }
    reverse_iterator rend() LIFETIME_BOUND { return reverse_iterator(begin()); }
    const_reverse_iterator rbegin() const LIFETIME_BOUND { return const_reverse_iterator(end()); }
    const_reverse_iterator rend() const LIFETIME_BOUND { return const_reverse_iterator(begin()); }
    
    template<typename U> bool contains(const U&) const;

    T& first() LIFETIME_BOUND { return m_buffer.capacitySpan()[m_start]; }
    const T& first() const LIFETIME_BOUND { return m_buffer.capacitySpan()[m_start]; }
    T takeFirst();

    T& last() LIFETIME_BOUND { return m_end ? m_buffer.capacitySpan()[m_end - 1] : m_buffer.capacitySpan().back(); }
    const T& last() const LIFETIME_BOUND { return m_end ? m_buffer.capacitySpan()[m_end - 1] : m_buffer.capacitySpan().back(); }
    T takeLast();

    void append(T&& value) { append<T>(std::forward<T>(value)); }
    template<typename U> void append(U&&);
    template<typename U> void prepend(U&&);
    void removeFirst();
    void removeLast();
    void remove(iterator&);
    void remove(const_iterator&);
    
    template<typename Func> size_t removeAllMatching(const Func&);
    template<typename Func> bool removeFirstMatching(const Func&);

    // This is a priority enqueue. The callback is given a value, and if it returns true, then this
    // will put the appended value before that value. It will keep bubbling until the callback returns
    // false or the value ends up at the head of the queue.
    template<typename U, typename Func>
    void appendAndBubble(U&&, const Func&);
    
    // Remove and return the first element for which the callback returns true. Returns a null version of
    // T if it the callback always returns false.
    template<typename Func>
    T takeFirst(NOESCAPE const Func&);

    // Remove and return the last element for which the callback returns true. Returns a null version of
    // T if it the callback always returns false.
    template<typename Func>
    T takeLast(NOESCAPE const Func&);

    void clear();

    template<typename Predicate> iterator findIf(NOESCAPE const Predicate&);
    template<typename Predicate> const_iterator findIf(NOESCAPE const Predicate&) const;
    template<typename Predicate> bool containsIf(NOESCAPE const Predicate& predicate) const
    {
        return findIf(predicate) != end();
    }

private:
    friend class DequeIteratorBase<T, inlineCapacity>;

    typedef VectorBuffer<T, inlineCapacity> Buffer;
    typedef VectorTypeOperations<T> TypeOperations;
    typedef DequeIteratorBase<T, inlineCapacity> IteratorBase;

    void remove(size_t position);
    void invalidateIterators();
    void destroyAll();
    void checkValidity() const;
    void checkIndexValidity(size_t) const;
    void expandCapacityIfNeeded();
    void expandCapacity();

    size_t m_start;
    size_t m_end;
    Buffer m_buffer;
#ifndef NDEBUG
    mutable IteratorBase* m_iterators;
#endif
};

template<typename T, size_t inlineCapacity = 0>
class DequeIteratorBase {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(DequeIteratorBase);
protected:
    DequeIteratorBase();
    DequeIteratorBase(const Deque<T, inlineCapacity>*, size_t);
    DequeIteratorBase(const DequeIteratorBase&);
    DequeIteratorBase& operator=(const DequeIteratorBase&);
    ~DequeIteratorBase();

    void assign(const DequeIteratorBase& other) { *this = other; }

    void increment(std::ptrdiff_t count = 1);
    void decrement();

    T* before() const;
    T* after() const;

    bool isEqual(const DequeIteratorBase&) const;

private:
    void addToIteratorsList();
    void removeFromIteratorsList();
    void checkValidity() const;
    void checkValidity(const DequeIteratorBase&) const;

    Deque<T, inlineCapacity>* m_deque;
    size_t m_index;

    friend class Deque<T, inlineCapacity>;

#ifndef NDEBUG
    mutable DequeIteratorBase* m_next;
    mutable DequeIteratorBase* m_previous;
#endif
};

template<typename T, size_t inlineCapacity = 0>
class DequeIterator : public DequeIteratorBase<T, inlineCapacity> {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(DequeIterator);
private:
    typedef DequeIteratorBase<T, inlineCapacity> Base;
    typedef DequeIterator<T, inlineCapacity> Iterator;

public:
    typedef ptrdiff_t difference_type;
    typedef T value_type;
    typedef T* pointer;
    typedef T& reference;
    typedef std::bidirectional_iterator_tag iterator_category;

    DequeIterator(Deque<T, inlineCapacity>* deque, size_t index)
        : Base(deque, index) { }

    DequeIterator(const Iterator& other) : Base(other) { }
    DequeIterator& operator=(const Iterator& other) { Base::assign(other); return *this; }

    T& operator*() const { return *Base::after(); }
    T* operator->() const { return Base::after(); }

    bool operator==(const Iterator& other) const { return Base::isEqual(other); }

    Iterator& operator++() { Base::increment(); return *this; }
    // postfix ++ intentionally omitted
    Iterator& operator--() { Base::decrement(); return *this; }
    // postfix -- intentionally omitted

    // Only forwarding + unsigned is supported.
    Iterator& operator+=(size_t count) { Base::increment(count); return *this; }
    Iterator operator+(size_t count) const { Iterator result(*this); result += count; return result; }
};

template<typename T, size_t inlineCapacity = 0>
class DequeConstIterator : public DequeIteratorBase<T, inlineCapacity> {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(DequeConstIterator);
private:
    typedef DequeIteratorBase<T, inlineCapacity> Base;
    typedef DequeConstIterator<T, inlineCapacity> Iterator;
    typedef DequeIterator<T, inlineCapacity> NonConstIterator;

public:
    typedef ptrdiff_t difference_type;
    typedef T value_type;
    typedef const T* pointer;
    typedef const T& reference;
    typedef std::bidirectional_iterator_tag iterator_category;

    DequeConstIterator(const Deque<T, inlineCapacity>* deque, size_t index)
        : Base(deque, index) { }

    DequeConstIterator(const Iterator& other) : Base(other) { }
    DequeConstIterator(const NonConstIterator& other) : Base(other) { }
    DequeConstIterator& operator=(const Iterator& other) { Base::assign(other); return *this; }
    DequeConstIterator& operator=(const NonConstIterator& other) { Base::assign(other); return *this; }

    const T& operator*() const { return *Base::after(); }
    const T* operator->() const { return Base::after(); }

    bool operator==(const Iterator& other) const { return Base::isEqual(other); }

    Iterator& operator++() { Base::increment(); return *this; }
    // postfix ++ intentionally omitted
    Iterator& operator--() { Base::decrement(); return *this; }
    // postfix -- intentionally omitted

    // Only forwarding + unsigned is supported.
    Iterator& operator+=(size_t count) { Base::increment(count); return *this; }
    Iterator operator+(size_t count) const { Iterator result(*this); result += count; return result; }
};

#ifdef NDEBUG
template<typename T, size_t inlineCapacity> inline void Deque<T, inlineCapacity>::checkValidity() const { }
template<typename T, size_t inlineCapacity> inline void Deque<T, inlineCapacity>::checkIndexValidity(size_t) const { }
template<typename T, size_t inlineCapacity> inline void Deque<T, inlineCapacity>::invalidateIterators() { }
#else
template<typename T, size_t inlineCapacity>
void Deque<T, inlineCapacity>::checkValidity() const
{
    // In this implementation a capacity of 1 would confuse append() and
    // other places that assume the index after capacity - 1 is 0.
    ASSERT(m_buffer.capacity() != 1);

    if (!m_buffer.capacity()) {
        ASSERT(!m_start);
        ASSERT(!m_end);
    } else {
        ASSERT(m_start < m_buffer.capacity());
        ASSERT(m_end < m_buffer.capacity());
    }
}

template<typename T, size_t inlineCapacity>
void Deque<T, inlineCapacity>::checkIndexValidity(size_t index) const
{
    ASSERT_UNUSED(index, index <= m_buffer.capacity());
    if (m_start <= m_end) {
        ASSERT(index >= m_start);
        ASSERT(index <= m_end);
    } else {
        ASSERT(index >= m_start || index <= m_end);
    }
}

template<typename T, size_t inlineCapacity>
void Deque<T, inlineCapacity>::invalidateIterators()
{
    IteratorBase* next;
    for (IteratorBase* p = m_iterators; p; p = next) {
        next = p->m_next;
        p->m_deque = 0;
        p->m_next = 0;
        p->m_previous = 0;
    }
    m_iterators = 0;
}
#endif

template<typename T, size_t inlineCapacity>
inline Deque<T, inlineCapacity>::Deque()
    : m_start(0)
    , m_end(0)
#ifndef NDEBUG
    , m_iterators(0)
#endif
{
    checkValidity();
}

template<typename T, size_t inlineCapacity>
inline Deque<T, inlineCapacity>::Deque(std::initializer_list<T> initializerList)
    : Deque()
{
    for (auto& element : initializerList)
        append(element);
}

template<typename T, size_t inlineCapacity>
inline Deque<T, inlineCapacity>::Deque(const Deque& other)
    : m_start(other.m_start)
    , m_end(other.m_end)
    , m_buffer(other.m_buffer.capacity())
#ifndef NDEBUG
    , m_iterators(0)
#endif
{
    auto otherBuffer = other.m_buffer.capacitySpan();
    if (m_start <= m_end)
        TypeOperations::uninitializedCopy(otherBuffer.subspan(m_start, m_end - m_start), m_buffer.capacitySpan().subspan(m_start));
    else {
        TypeOperations::uninitializedCopy(otherBuffer.first(m_end), m_buffer.capacitySpan());
        TypeOperations::uninitializedCopy(otherBuffer.subspan(m_start, m_buffer.capacity() - m_start), m_buffer.capacitySpan().subspan(m_start));
    }
}

template<typename T, size_t inlineCapacity>
inline Deque<T, inlineCapacity>::Deque(Deque&& other)
    : Deque()
{
    swap(other);
}

template<typename T, size_t inlineCapacity>
inline auto Deque<T, inlineCapacity>::operator=(const Deque& other) -> Deque&
{
    // FIXME: This is inefficient if we're using an inline buffer and T is
    // expensive to copy since it will copy the buffer twice instead of once.
    Deque<T, inlineCapacity> copy(other);
    swap(copy);
    return *this;
}

template<typename T, size_t inlineCapacity>
inline auto Deque<T, inlineCapacity>::operator=(Deque&& other) -> Deque&
{
    swap(other);
    return *this;
}

template<typename T, size_t inlineCapacity>
inline void Deque<T, inlineCapacity>::destroyAll()
{
    auto span = m_buffer.capacitySpan();
    if (m_start <= m_end)
        TypeOperations::destruct(span.subspan(m_start, m_end - m_start));
    else {
        TypeOperations::destruct(span.first(m_end));
        TypeOperations::destruct(span.subspan(m_start));
    }
}

template<typename T, size_t inlineCapacity>
inline Deque<T, inlineCapacity>::~Deque()
{
    checkValidity();
    invalidateIterators();
    destroyAll();
}

template<typename T, size_t inlineCapacity>
inline void Deque<T, inlineCapacity>::swap(Deque<T, inlineCapacity>& other)
{
    checkValidity();
    other.checkValidity();
    invalidateIterators();
    std::swap(m_start, other.m_start);
    std::swap(m_end, other.m_end);
    m_buffer.swap(other.m_buffer, 0, 0);
    checkValidity();
    other.checkValidity();
}

template<typename T, size_t inlineCapacity>
inline void Deque<T, inlineCapacity>::clear()
{
    checkValidity();
    invalidateIterators();
    destroyAll();
    m_start = 0;
    m_end = 0;
    m_buffer.deallocateBuffer(m_buffer.buffer());
    checkValidity();
}

template<typename T, size_t inlineCapacity>
template<typename Predicate>
inline auto Deque<T, inlineCapacity>::findIf(NOESCAPE const Predicate& predicate) -> iterator
{
    return std::find_if(begin(), end(), predicate);
}

template<typename T, size_t inlineCapacity>
template<typename Predicate>
inline auto Deque<T, inlineCapacity>::findIf(NOESCAPE const Predicate& predicate) const -> const_iterator
{
    return std::find_if(begin(), end(), predicate);
}

template<typename T, size_t inlineCapacity>
inline void Deque<T, inlineCapacity>::expandCapacityIfNeeded()
{
    if (m_start) {
        if (m_end + 1 != m_start)
            return;
    } else if (m_end) {
        if (m_end != m_buffer.capacity() - 1)
            return;
    } else if (m_buffer.capacity())
        return;

    expandCapacity();
}

template<typename T, size_t inlineCapacity>
void Deque<T, inlineCapacity>::expandCapacity()
{
    checkValidity();
    size_t oldCapacity = m_buffer.capacity();
    auto oldBuffer = m_buffer.capacitySpan();
    m_buffer.allocateBuffer(std::max(static_cast<size_t>(16), oldCapacity + oldCapacity / 4 + 1));
    if (m_start <= m_end)
        TypeOperations::move(oldBuffer.subspan(m_start, m_end - m_start), m_buffer.capacitySpan().subspan(m_start));
    else {
        TypeOperations::move(oldBuffer.first(m_end), m_buffer.capacitySpan());
        size_t newStart = m_buffer.capacity() - (oldCapacity - m_start);
        TypeOperations::move(oldBuffer.subspan(m_start, oldCapacity - m_start), m_buffer.capacitySpan().subspan(newStart));
        m_start = newStart;
    }
    m_buffer.deallocateBuffer(oldBuffer.data());
    checkValidity();
}

template<typename T, size_t inlineCapacity>
template<typename U>
bool Deque<T, inlineCapacity>::contains(const U& searchValue) const
{
    auto endIterator = end();
    return std::find(begin(), endIterator, searchValue) != endIterator;
}

template<typename T, size_t inlineCapacity>
inline auto Deque<T, inlineCapacity>::takeFirst() -> T
{
    T oldFirst = WTFMove(first());
    removeFirst();
    return oldFirst;
}

template<typename T, size_t inlineCapacity>
inline auto Deque<T, inlineCapacity>::takeLast() -> T
{
    T oldLast = WTFMove(last());
    removeLast();
    return oldLast;
}

template<typename T, size_t inlineCapacity> template<typename U>
inline void Deque<T, inlineCapacity>::append(U&& value)
{
    checkValidity();
    expandCapacityIfNeeded();
    new (NotNull, std::addressof(m_buffer.capacitySpan()[m_end])) T(std::forward<U>(value));
    if (m_end == m_buffer.capacity() - 1)
        m_end = 0;
    else
        ++m_end;
    checkValidity();
}

template<typename T, size_t inlineCapacity> template<typename U>
inline void Deque<T, inlineCapacity>::prepend(U&& value)
{
    checkValidity();
    expandCapacityIfNeeded();
    if (!m_start)
        m_start = m_buffer.capacity() - 1;
    else
        --m_start;
    new (NotNull, std::addressof(m_buffer.capacitySpan()[m_start])) T(std::forward<U>(value));
    checkValidity();
}

template<typename T, size_t inlineCapacity>
inline void Deque<T, inlineCapacity>::removeFirst()
{
    checkValidity();
    invalidateIterators();
    RELEASE_ASSERT(!isEmpty());
    TypeOperations::destruct(m_buffer.capacitySpan().subspan(m_start, 1));
    if (m_start == m_buffer.capacity() - 1)
        m_start = 0;
    else
        ++m_start;
    checkValidity();
}

template<typename T, size_t inlineCapacity>
inline void Deque<T, inlineCapacity>::removeLast()
{
    checkValidity();
    invalidateIterators();
    RELEASE_ASSERT(!isEmpty());
    if (!m_end)
        m_end = m_buffer.capacity() - 1;
    else
        --m_end;
    TypeOperations::destruct(m_buffer.capacitySpan().subspan(m_end, 1));
    checkValidity();
}

template<typename T, size_t inlineCapacity>
inline void Deque<T, inlineCapacity>::remove(iterator& it)
{
    it.checkValidity();
    remove(it.m_index);
}

template<typename T, size_t inlineCapacity>
inline void Deque<T, inlineCapacity>::remove(const_iterator& it)
{
    it.checkValidity();
    remove(it.m_index);
}

template<typename T, size_t inlineCapacity>
inline void Deque<T, inlineCapacity>::remove(size_t position)
{
    if (position == m_end)
        return;

    checkValidity();
    invalidateIterators();

    auto buffer = m_buffer.capacitySpan();
    TypeOperations::destruct(buffer.subspan(position, 1));

    // Find which segment of the circular buffer contained the remove element, and only move elements in that part.
    if (position >= m_start) {
        TypeOperations::moveOverlapping(buffer.subspan(m_start, position - m_start), buffer.subspan(m_start + 1));
        m_start = (m_start + 1) % m_buffer.capacity();
    } else {
        TypeOperations::moveOverlapping(buffer.subspan(position + 1, m_end - (position + 1)), buffer.subspan(position));
        m_end = (m_end - 1 + m_buffer.capacity()) % m_buffer.capacity();
    }
    checkValidity();
}

template<typename T, size_t inlineCapacity>
template<typename Func>
inline size_t Deque<T, inlineCapacity>::removeAllMatching(const Func& func)
{
    auto oldSize = size();
    for (size_t i = 0; i < oldSize; ++i) {
        auto value = takeFirst();
        if (!func(value))
            append(WTFMove(value));
    }
    return size() - oldSize;
}

template<typename T, size_t inlineCapacity>
template<typename Func>
inline bool Deque<T, inlineCapacity>::removeFirstMatching(const Func& func)
{
    for (auto iter = begin(); iter != end(); ++iter) {
        if (func(*iter)) {
            remove(iter);
            return true;
        }
    }
    return false;
}

template<typename T, size_t inlineCapacity>
template<typename U, typename Func>
inline void Deque<T, inlineCapacity>::appendAndBubble(U&& value, const Func& func)
{
    append(std::forward<U>(value));
    iterator begin = this->begin();
    iterator iter = end();
    --iter;
    while (iter != begin) {
        iterator prev = iter;
        --prev;
        if (!func(*prev))
            return;
        std::swap(*prev, *iter);
        iter = prev;
    }
}

template<typename T, size_t inlineCapacity>
template<typename Func>
inline T Deque<T, inlineCapacity>::takeFirst(NOESCAPE const Func& func)
{
    unsigned count = 0;
    unsigned size = this->size();
    while (count < size) {
        T candidate = takeFirst();
        if (func(candidate)) {
            while (count--)
                prepend(takeLast());
            return candidate;
        }
        count++;
        append(WTFMove(candidate));
    }
    return T();
}

template<typename T, size_t inlineCapacity>
template<typename Func>
inline T Deque<T, inlineCapacity>::takeLast(NOESCAPE const Func& func)
{
    unsigned count = 0;
    unsigned size = this->size();
    while (count < size) {
        T candidate = takeLast();
        if (func(candidate)) {
            while (count--)
                append(takeFirst());
            return candidate;
        }
        count++;
        prepend(WTFMove(candidate));
    }
    return T();
}

#ifdef NDEBUG
template<typename T, size_t inlineCapacity> inline void DequeIteratorBase<T, inlineCapacity>::checkValidity() const { }
template<typename T, size_t inlineCapacity> inline void DequeIteratorBase<T, inlineCapacity>::checkValidity(const DequeIteratorBase<T, inlineCapacity>&) const { }
template<typename T, size_t inlineCapacity> inline void DequeIteratorBase<T, inlineCapacity>::addToIteratorsList() { }
template<typename T, size_t inlineCapacity> inline void DequeIteratorBase<T, inlineCapacity>::removeFromIteratorsList() { }
#else
template<typename T, size_t inlineCapacity>
void DequeIteratorBase<T, inlineCapacity>::checkValidity() const
{
    ASSERT(m_deque);
    m_deque->checkIndexValidity(m_index);
}

template<typename T, size_t inlineCapacity>
void DequeIteratorBase<T, inlineCapacity>::checkValidity(const DequeIteratorBase& other) const
{
    checkValidity();
    other.checkValidity();
    ASSERT(m_deque == other.m_deque);
}

template<typename T, size_t inlineCapacity>
void DequeIteratorBase<T, inlineCapacity>::addToIteratorsList()
{
    if (!m_deque)
        m_next = 0;
    else {
        m_next = m_deque->m_iterators;
        m_deque->m_iterators = this;
        if (m_next)
            m_next->m_previous = this;
    }
    m_previous = 0;
}

template<typename T, size_t inlineCapacity>
void DequeIteratorBase<T, inlineCapacity>::removeFromIteratorsList()
{
    if (!m_deque) {
        ASSERT(!m_next);
        ASSERT(!m_previous);
    } else {
        if (m_next) {
            ASSERT(m_next->m_previous == this);
            m_next->m_previous = m_previous;
        }
        if (m_previous) {
            ASSERT(m_deque->m_iterators != this);
            ASSERT(m_previous->m_next == this);
            m_previous->m_next = m_next;
        } else {
            ASSERT(m_deque->m_iterators == this);
            m_deque->m_iterators = m_next;
        }
    }
    m_next = 0;
    m_previous = 0;
}
#endif

template<typename T, size_t inlineCapacity>
inline DequeIteratorBase<T, inlineCapacity>::DequeIteratorBase()
    : m_deque(0)
{
}

template<typename T, size_t inlineCapacity>
inline DequeIteratorBase<T, inlineCapacity>::DequeIteratorBase(const Deque<T, inlineCapacity>* deque, size_t index)
    : m_deque(const_cast<Deque<T, inlineCapacity>*>(deque))
    , m_index(index)
{
    addToIteratorsList();
    checkValidity();
}

template<typename T, size_t inlineCapacity>
inline DequeIteratorBase<T, inlineCapacity>::DequeIteratorBase(const DequeIteratorBase& other)
    : m_deque(other.m_deque)
    , m_index(other.m_index)
{
    addToIteratorsList();
    checkValidity();
}

template<typename T, size_t inlineCapacity>
inline DequeIteratorBase<T, inlineCapacity>& DequeIteratorBase<T, inlineCapacity>::operator=(const DequeIteratorBase& other)
{
    other.checkValidity();
    removeFromIteratorsList();

    m_deque = other.m_deque;
    m_index = other.m_index;
    addToIteratorsList();
    checkValidity();
    return *this;
}

template<typename T, size_t inlineCapacity>
inline DequeIteratorBase<T, inlineCapacity>::~DequeIteratorBase()
{
#ifndef NDEBUG
    removeFromIteratorsList();
    m_deque = 0;
#endif
}

template<typename T, size_t inlineCapacity>
inline bool DequeIteratorBase<T, inlineCapacity>::isEqual(const DequeIteratorBase& other) const
{
    checkValidity(other);
    return m_index == other.m_index;
}

template<typename T, size_t inlineCapacity>
inline void DequeIteratorBase<T, inlineCapacity>::increment(std::ptrdiff_t count)
{
    checkValidity();
    if (!count)
        return;
    ASSERT(m_index != m_deque->m_end);
    size_t capacity = m_deque->m_buffer.capacity();
    ASSERT(capacity);
    m_index += count;
    do {
        if (m_index < capacity)
            break;
        m_index -= capacity;
    } while (true);
    checkValidity();
}

template<typename T, size_t inlineCapacity>
inline void DequeIteratorBase<T, inlineCapacity>::decrement()
{
    checkValidity();
    ASSERT(m_index != m_deque->m_start);
    ASSERT(m_deque->m_buffer.capacity());
    if (!m_index)
        m_index = m_deque->m_buffer.capacity() - 1;
    else
        --m_index;
    checkValidity();
}

template<typename T, size_t inlineCapacity>
inline T* DequeIteratorBase<T, inlineCapacity>::after() const
{
    checkValidity();
    ASSERT(m_index != m_deque->m_end);
    return std::addressof(m_deque->m_buffer.capacitySpan()[m_index]);
}

template<typename T, size_t inlineCapacity>
inline T* DequeIteratorBase<T, inlineCapacity>::before() const
{
    checkValidity();
    ASSERT(m_index != m_deque->m_start);
    if (!m_index)
        return std::addressof(m_deque->m_buffer.capacitySpan()[m_deque->m_buffer.capacity() - 1]);
    return std::addressof(m_deque->m_buffer.capacitySpan()[m_index - 1]);
}

} // namespace WTF
