/*
 * Copyright (C) 2023-2025 Apple Inc. All rights reserved.
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

#include "config.h"
#include "CommandEvent.h"

#include "Element.h"
#include "TreeScope.h"

#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(CommandEvent);

CommandEvent::CommandEvent()
    : Event(EventInterfaceType::CommandEvent)
{
}

CommandEvent::CommandEvent(const AtomString& type, const CommandEvent::Init& initializer, IsTrusted isTrusted)
    : Event(EventInterfaceType::CommandEvent, type, initializer, isTrusted)
    , m_source(initializer.source)
    , m_command(initializer.command)
{
}

Ref<CommandEvent> CommandEvent::create(const AtomString& eventType, const CommandEvent::Init& init, IsTrusted isTrusted)
{
    return adoptRef(*new CommandEvent(eventType, init, isTrusted));
}

Ref<CommandEvent> CommandEvent::createForBindings()
{
    return adoptRef(*new CommandEvent);
}

bool CommandEvent::isCommandEvent() const
{
    return true;
}

RefPtr<Element> CommandEvent::source() const
{
    if (!m_source)
        return nullptr;

    if (RefPtr target = dynamicDowncast<Node>(currentTarget())) {
        Ref treeScope = target->treeScope();
        Ref node = treeScope->retargetToScope(*m_source.get());
        return &downcast<Element>(node).get();
    }
    return m_source;
}

} // namespace WebCore
