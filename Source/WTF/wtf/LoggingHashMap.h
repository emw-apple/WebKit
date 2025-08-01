/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
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

#include <wtf/DataLog.h>
#include <wtf/HashMap.h>
#include <wtf/LoggingHashID.h>
#include <wtf/LoggingHashTraits.h>

namespace WTF {

template<
    const char* typeArguments,
    typename KeyArg, typename MappedArg, typename HashArg = DefaultHash<KeyArg>,
    typename KeyTraitsArg = HashTraits<KeyArg>, typename MappedTraitsArg = HashTraits<MappedArg>,
    typename LoggingKeyTraits = LoggingHashKeyTraits<KeyArg>,
    typename LoggingValueTraits = LoggingHashValueTraits<MappedArg>>
class LoggingHashMap final {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(LoggingHashMap);

public:
    typedef WTF::HashMap<KeyArg, MappedArg, HashArg, KeyTraitsArg, MappedTraitsArg> HashMap;
    
    typedef typename HashMap::KeyType KeyType;
    typedef typename HashMap::MappedType MappedType;
    typedef typename HashMap::KeyValuePairType KeyValuePairType;
    
    typedef typename HashMap::iterator iterator;
    typedef typename HashMap::const_iterator const_iterator;
    typedef typename HashMap::AddResult AddResult;

private:
    typedef typename HashMap::MappedTraits::PeekType MappedPeekType;

public:

    LoggingHashMap()
    {
        dataLog("auto* ", m_id, " = new HashMap<", typeArguments, ">();\n");
    }
    
    ~LoggingHashMap()
    {
        dataLog("delete ", m_id, ";\n");
    }
    
    LoggingHashMap(const LoggingHashMap& other)
        : m_map(other.m_map)
    {
        dataLog("auto* ", m_id, " = new HashMap(*", other.m_id, ");");
    }
    
    LoggingHashMap(LoggingHashMap&& other)
        : m_map(other.m_map)
    {
        dataLog("auto* ", m_id, " = new HashMap(WTFMove(*", other.m_id, "));");
    }
    
    LoggingHashMap& operator=(const LoggingHashMap& other)
    {
        dataLog("*", m_id, " = *", other.m_id, ";\n");
        m_map = other.m_map;
    }
    
    LoggingHashMap& operator=(LoggingHashMap&& other)
    {
        dataLog("*", m_id, " = WTFMove(*", other.m_id, ");\n");
        m_map = WTFMove(other.m_map);
    }
    
    void swap(LoggingHashMap& other)
    {
        dataLog(m_id, "->swap(*", RawPointer(&other), ");\n");
        m_map.swap(other.m_map);
    }
    
    // A bunch of stuff does not get logged.
    unsigned size() const { return m_map.size(); }
    unsigned capacity() const { return m_map.capacity(); }
    bool isEmpty() const { return m_map.isEmpty(); }
    
    iterator begin() { return m_map.begin(); }
    iterator end() { return m_map.end(); }
    const_iterator begin() const { return m_map.begin(); }
    const_iterator end() const { return m_map.end(); }

    iterator random() { return m_map.random(); }
    const_iterator random() const { return m_map.random(); }
    
    auto keys() { return m_map.keys(); }
    auto keys() const { return m_map.keys(); }
    auto values() { return m_map.values(); }
    auto values() const { return m_map.values(); }
    
    iterator find(const KeyType& key)
    {
        StringPrintStream string;
        string.print("{\n");
        string.print("    auto iter = ", m_id, "->find(");
        LoggingKeyTraits::print(string, key);
        string.print(");\n");
        iterator result = m_map.find(key);
        if (result == m_map.end())
            string.print("    RELEASE_ASSERT(iter == ", m_id, "->end());\n");
        else
            string.print("    RELEASE_ASSERT(iter != ", m_id, "->end());\n");
        string.print("}\n");
        dataLog(string.toCString());
        return result;
    }
    
    const_iterator find(const KeyType& key) const
    {
        StringPrintStream string;
        string.print("{\n");
        string.print("    auto iter = ", m_id, "->find(");
        LoggingKeyTraits::print(string, key);
        string.print(");\n");
        const_iterator result = m_map.find(key);
        if (result == m_map.end())
            string.print("    RELEASE_ASSERT(iter == ", m_id, "->end());\n");
        else
            string.print("    RELEASE_ASSERT(iter != ", m_id, "->end());\n");
        string.print("}\n");
        dataLog(string.toCString());
        return result;
    }
    
    bool contains(const KeyType& key) const
    {
        return find(key) != end();
    }
    
    MappedPeekType get(const KeyType& key) const
    {
        find(key);
        return m_map.get(key);
    }
    
    MappedPeekType inlineGet(const KeyType& key) const
    {
        find(key);
        return m_map.inlineGet(key);
    }
    
    template<typename PassedType>
    AddResult set(const KeyType& key, PassedType&& passedValue)
    {
        StringPrintStream string;
        string.print(m_id, "->set(");
        LoggingKeyTraits::print(string, key);
        string.print(", ");
        LoggingValueTraits::print(string, passedValue);
        string.print(");\n");
        dataLog(string.toCString());
        return set(key, std::forward<PassedType>(passedValue));
    }
    
    template<typename PassedType>
    AddResult set(KeyType&& key, PassedType&& passedValue)
    {
        StringPrintStream string;
        string.print(m_id, "->set(");
        LoggingKeyTraits::print(string, key);
        string.print(", ");
        LoggingValueTraits::print(string, passedValue);
        string.print(");\n");
        dataLog(string.toCString());
        return set(WTFMove(key), std::forward<PassedType>(passedValue));
    }
    
    template<typename PassedType>
    AddResult add(const KeyType& key, PassedType&& passedValue)
    {
        StringPrintStream string;
        string.print(m_id, "->add(");
        LoggingKeyTraits::print(string, key);
        string.print(", ");
        LoggingValueTraits::print(string, passedValue);
        string.print(");\n");
        dataLog(string.toCString());
        return add(key, std::forward<PassedType>(passedValue));
    }
    
    template<typename PassedType>
    AddResult add(KeyType&& key, PassedType&& passedValue)
    {
        StringPrintStream string;
        string.print(m_id, "->add(");
        LoggingKeyTraits::print(string, key);
        string.print(", ");
        LoggingValueTraits::print(string, passedValue);
        string.print(");\n");
        dataLog(string.toCString());
        return add(WTFMove(key), std::forward<PassedType>(passedValue));
    }
    
    template<typename PassedType>
    AddResult fastAdd(const KeyType& key, PassedType&& passedValue)
    {
        return add(key, std::forward<PassedType>(passedValue));
    }
    
    template<typename PassedType>
    AddResult fastAdd(KeyType&& key, PassedType&& passedValue)
    {
        return add(WTFMove(key), std::forward<PassedType>(passedValue));
    }
    
    template<typename Func>
    AddResult ensure(const KeyType& key, Func&& func)
    {
        StringPrintStream string;
        string.print(m_id, "->ensure(");
        LoggingKeyTraits::print(string, key);
        string.print(", ");
        string.print("[] () { return ");
        bool didCallFunctor = false;
        auto result = m_map.ensure(
            key,
            [&] () {
                didCallFunctor = true;
                auto result = func();
                LoggingValueTraits::print(string, result);
                return result;
            });
        if (!didCallFunctor)
            LoggingValueTraits::print(string, MappedTraitsArg::emptyValue());
        string.print("; });\n");
        dataLog(string.toCString());
        return result;
    }
    
    template<typename Func>
    AddResult ensure(KeyType&& key, Func&& func)
    {
        StringPrintStream string;
        string.print(m_id, "->ensure(");
        LoggingKeyTraits::print(string, key);
        string.print(", ");
        string.print("[] () { return ");
        bool didCallFunctor = false;
        auto result = m_map.ensure(
            WTFMove(key),
            [&] () {
                didCallFunctor = true;
                auto result = func();
                LoggingValueTraits::print(string, result);
                return result;
            });
        if (!didCallFunctor)
            LoggingValueTraits::print(string, MappedTraitsArg::emptyValue());
        string.print("; });\n");
        dataLog(string.toCString());
        return result;
    }
    
    bool remove(const KeyType& key)
    {
        StringPrintStream string;
        string.print(m_id, "->remove(");
        LoggingKeyTraits::print(string, key);
        string.print(");\n");
        dataLog(string.toCString());
        return m_map.remove(key);
    }
    
    bool remove(iterator iter)
    {
        // FIXME: It would be nice if we could do better than this.
        if (iter == end())
            return false;
        return remove(iter->key);
    }

    // FIXME: Implement removeIf().
    
    void clear()
    {
        dataLog(m_id, "->clear();\n");
        m_map.clear();
    }
    
    // FIXME: Implement the no-convert overloads.
    
private:
    HashMap m_map;
    LoggingHashID m_id;
};

} // namespace WTF

using WTF::LoggingHashMap;
