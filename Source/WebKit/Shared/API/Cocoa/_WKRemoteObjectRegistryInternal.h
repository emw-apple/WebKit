/*
 * Copyright (C) 2013 Apple Inc. All rights reserved.
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

#import "_WKRemoteObjectRegistry.h"

namespace IPC {
class MessageSender;
}

namespace WebKit {
class RemoteObjectInvocation;
class RemoteObjectRegistry;
class UserData;
class WebPage;
class WebPageProxy;
}

@interface _WKRemoteObjectRegistry ()

@property (nonatomic, readonly) WebKit::RemoteObjectRegistry& remoteObjectRegistry;

- (id)_initWithWebPage:(std::reference_wrapper<WebKit::WebPage>)messageSender;
- (id)_initWithWebPageProxy:(std::reference_wrapper<WebKit::WebPageProxy>)messageSender;
- (void)_invalidate;

- (void)_sendInvocation:(NSInvocation *)invocation interface:(_WKRemoteObjectInterface *)interface;
- (void)_invokeMethod:(const WebKit::RemoteObjectInvocation&)invocation;

- (void)_callReplyWithID:(uint64_t)replyID blockInvocation:(const WebKit::UserData&)blockInvocation;
- (void)_releaseReplyWithID:(uint64_t)replyID;

@end
