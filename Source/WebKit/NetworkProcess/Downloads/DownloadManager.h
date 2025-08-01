/*
 * Copyright (C) 2010-2016 Apple Inc. All rights reserved.
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

#include "DownloadID.h"
#include "DownloadMap.h"
#include "PendingDownload.h"
#include "PolicyDecision.h"
#include "SandboxExtension.h"
#include "UseDownloadPlaceholder.h"
#include <WebCore/LocalFrameLoaderClient.h>
#include <WebCore/NotImplemented.h>
#include <wtf/AbstractRefCounted.h>
#include <wtf/CheckedRef.h>
#include <wtf/Forward.h>
#include <wtf/HashMap.h>
#include <wtf/Noncopyable.h>

namespace PAL {
class SessionID;
}

namespace WebCore {
class BlobDataFileReference;
class ResourceRequest;
class ResourceResponse;

enum class FromDownloadAttribute : bool;
}

namespace IPC {
class Connection;
}

namespace WebKit {

class AuthenticationManager;
class Download;
class NetworkConnectionToWebProcess;
class NetworkDataTask;
class NetworkLoad;
class PendingDownload;

enum class CallDownloadDidStart : bool { No, Yes };

class DownloadManager : public CanMakeCheckedPtr<DownloadManager> {
    WTF_MAKE_NONCOPYABLE(DownloadManager);
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(DownloadManager);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(DownloadManager);
public:
    class Client : public AbstractRefCounted {
    public:
        virtual ~Client() { }

        // CheckedPtr interface
        virtual uint32_t checkedPtrCount() const = 0;
        virtual uint32_t checkedPtrCountWithoutThreadCheck() const = 0;
        virtual void incrementCheckedPtrCount() const = 0;
        virtual void decrementCheckedPtrCount() const = 0;

        virtual void didCreateDownload() = 0;
        virtual void didDestroyDownload() = 0;
        virtual IPC::Connection* downloadProxyConnection() = 0;
        virtual IPC::Connection* parentProcessConnectionForDownloads() = 0;
        RefPtr<IPC::Connection> protectedParentProcessConnectionForDownloads();
        virtual AuthenticationManager& downloadsAuthenticationManager() = 0;
        Ref<AuthenticationManager> protectedDownloadsAuthenticationManager();
        virtual NetworkSession* networkSession(PAL::SessionID) const = 0;
    };

    explicit DownloadManager(Client&);
    ~DownloadManager();

    void startDownload(PAL::SessionID, DownloadID, const WebCore::ResourceRequest&, const std::optional<WebCore::SecurityOriginData>& topOrigin, std::optional<NavigatingToAppBoundDomain>, const String& suggestedName = { }, WebCore::FromDownloadAttribute = WebCore::FromDownloadAttribute::No, std::optional<WebCore::FrameIdentifier> frameID = std::nullopt, std::optional<WebCore::PageIdentifier> = std::nullopt, std::optional<WebCore::ProcessIdentifier> = std::nullopt);
    void dataTaskBecameDownloadTask(DownloadID, Ref<Download>&&);
    void convertNetworkLoadToDownload(DownloadID, Ref<NetworkLoad>&&, ResponseCompletionHandler&&,  Vector<RefPtr<WebCore::BlobDataFileReference>>&&, const WebCore::ResourceRequest&, const WebCore::ResourceResponse&);
    void downloadDestinationDecided(DownloadID, Ref<NetworkDataTask>&&);

    void resumeDownload(PAL::SessionID, DownloadID, std::span<const uint8_t> resumeData, const String& path, SandboxExtension::Handle&&, CallDownloadDidStart, std::span<const uint8_t> activityAccessToken);

    void cancelDownload(DownloadID, CompletionHandler<void(std::span<const uint8_t>)>&&);
#if PLATFORM(COCOA)
#if HAVE(MODERN_DOWNLOADPROGRESS)
    void publishDownloadProgress(DownloadID, const URL&, std::span<const uint8_t> bookmarkData, WebKit::UseDownloadPlaceholder, std::span<const uint8_t> activityAccessToken);
#else
    void publishDownloadProgress(DownloadID, const URL&, SandboxExtension::Handle&&);
#endif
#endif
    
    Download* download(DownloadID);

    void downloadFinished(Download&);
    bool isDownloading() const { return !m_downloads.isEmpty(); }

    void applicationDidEnterBackground();
    void applicationWillEnterForeground();

    void didCreateDownload();
    void didDestroyDownload();

    IPC::Connection* downloadProxyConnection();
    AuthenticationManager& downloadsAuthenticationManager();
    
    Client& client() { return m_client; }
    Ref<Client> protectedClient() { return m_client.get(); }

private:
    CheckedRef<Client> m_client;
    HashMap<DownloadID, Ref<PendingDownload>> m_pendingDownloads;
    HashMap<DownloadID, RefPtr<NetworkDataTask>> m_downloadsAfterDestinationDecided;
    DownloadMap m_downloads;
};

} // namespace WebKit
