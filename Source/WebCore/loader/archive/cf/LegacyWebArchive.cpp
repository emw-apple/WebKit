/*
 * Copyright (C) 2008-2025 Apple Inc. All rights reserved.
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

#include "config.h"
#include "LegacyWebArchive.h"

#include "BoundaryPointInlines.h"
#include "CSSImportRule.h"
#include "CSSSerializationContext.h"
#include "CachedResource.h"
#include "DeprecatedGlobalSettings.h"
#include "Document.h"
#include "DocumentLoader.h"
#include "Editor.h"
#include "EditorClient.h"
#include "FrameDestructionObserverInlines.h"
#include "FrameInlines.h"
#include "FrameLoader.h"
#include "FrameSelection.h"
#include "FrameTree.h"
#include "HTMLAttachmentElement.h"
#include "HTMLFrameElement.h"
#include "HTMLFrameOwnerElement.h"
#include "HTMLIFrameElement.h"
#include "HTMLNames.h"
#include "HTMLObjectElement.h"
#include "Image.h"
#include "LocalFrame.h"
#include "LocalFrameInlines.h"
#include "Logging.h"
#include "MIMETypeRegistry.h"
#include "MemoryCache.h"
#include "Page.h"
#include "SerializedAttachmentData.h"
#include "Settings.h"
#include "SharedBuffer.h"
#include "StyleSheet.h"
#include "StyleSheetList.h"
#include "markup.h"
#include <algorithm>
#include <wtf/ListHashSet.h>
#include <wtf/RetainPtr.h>
#include <wtf/URLHash.h>
#include <wtf/URLParser.h>
#include <wtf/text/CString.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringBuilder.h>

namespace WebCore {

static constexpr unsigned maxFileNameSizeInBytes = 255;
static constexpr char defaultFileName[] = "file";
static const CFStringRef LegacyWebArchiveMainResourceKey = CFSTR("WebMainResource");
static const CFStringRef LegacyWebArchiveSubresourcesKey = CFSTR("WebSubresources");
static const CFStringRef LegacyWebArchiveSubframeArchivesKey = CFSTR("WebSubframeArchives");
static const CFStringRef LegacyWebArchiveResourceDataKey = CFSTR("WebResourceData");
static const CFStringRef LegacyWebArchiveResourceFrameNameKey = CFSTR("WebResourceFrameName");
static const CFStringRef LegacyWebArchiveResourceMIMETypeKey = CFSTR("WebResourceMIMEType");
static const CFStringRef LegacyWebArchiveResourceURLKey = CFSTR("WebResourceURL");
static const CFStringRef LegacyWebArchiveResourceFilePathKey = CFSTR("WebResourceFilePath");
static const CFStringRef LegacyWebArchiveResourceTextEncodingNameKey = CFSTR("WebResourceTextEncodingName");
static const CFStringRef LegacyWebArchiveResourceResponseKey = CFSTR("WebResourceResponse");
static const CFStringRef LegacyWebArchiveResourceResponseVersionKey = CFSTR("WebResourceResponseVersion");

static bool isUnreservedURICharacter(char16_t character)
{
    return isASCIIAlphanumeric(character) || character == '-' || character == '.' || character == '_' || character == '~';
}

static String getFileNameFromURIComponent(StringView input)
{
    auto decodedInput = WTF::URLParser::formURLDecode(input);
    if (!decodedInput)
        return { };

    unsigned length = decodedInput->length();
    if (!length)
        return { };

    StringBuilder result;
    result.reserveCapacity(length);
    for (unsigned index = 0; index < length; ++index) {
        char16_t character = decodedInput->characterAt(index);
        if (isUnreservedURICharacter(character)) {
            result.append(character);
            continue;
        }
        result.append('-');
    }

    return result.toString();
}

static String generateValidFileName(const URL& url, const HashSet<String>& existingFileNames, const String& extension = { })
{
    String suffix = extension.isEmpty() ? emptyString() : makeString('.', extension);
    auto extractedFileName = getFileNameFromURIComponent(url.lastPathComponent());
    if (extractedFileName.endsWith(suffix))
        extractedFileName = extractedFileName.left(extractedFileName.length() - suffix.length());
    auto fileName = extractedFileName.isEmpty() ? String::fromLatin1(defaultFileName) : extractedFileName;

    RELEASE_ASSERT(suffix.length() < maxFileNameSizeInBytes);
    unsigned maxUniqueFileNameLength = maxFileNameSizeInBytes - suffix.length();
    String uniqueFileName;

    unsigned count = 0;
    do {
        uniqueFileName = fileName;
        if (count)
            uniqueFileName = makeString(fileName, '-', count);
        if (uniqueFileName.sizeInBytes() > maxUniqueFileNameLength)
            uniqueFileName = uniqueFileName.right(maxUniqueFileNameLength);
        uniqueFileName = makeString(uniqueFileName, suffix);
        ++count;
    } while (existingFileNames.contains(uniqueFileName));

    return uniqueFileName;
}

RetainPtr<CFDictionaryRef> LegacyWebArchive::createPropertyListRepresentation(ArchiveResource* resource, MainResourceStatus isMainResource)
{
    if (!resource) {
        // The property list representation of a null/empty WebResource has the following 3 objects stored as nil.
        // FIXME: 0 is not serializable. Presumably we need to use kCFNull here instead for compatibility.
        // FIXME: But why do we need to support a resource of 0? Who relies on that?
        RetainPtr<CFMutableDictionaryRef> propertyList = adoptCF(CFDictionaryCreateMutable(0, 3, 0, 0));
        CFDictionarySetValue(propertyList.get(), LegacyWebArchiveResourceDataKey, 0);
        CFDictionarySetValue(propertyList.get(), LegacyWebArchiveResourceURLKey, 0);
        CFDictionarySetValue(propertyList.get(), LegacyWebArchiveResourceMIMETypeKey, 0);
        return propertyList;
    }

    auto propertyList = adoptCF(CFDictionaryCreateMutable(0, 6, 0, &kCFTypeDictionaryValueCallBacks));

    // Resource data can be empty, but must be represented by an empty CFDataRef
    Ref data = resource->data();

    CFDictionarySetValue(propertyList.get(), LegacyWebArchiveResourceDataKey, data->makeContiguous()->createCFData().get());

    // Resource URL cannot be null
    if (auto cfURL = resource->url().string().createCFString())
        CFDictionarySetValue(propertyList.get(), LegacyWebArchiveResourceURLKey, cfURL.get());
    else {
        LOG(Archives, "LegacyWebArchive - NULL resource URL is invalid - returning null property list");
        return nullptr;
    }

    auto& filePath = resource->relativeFilePath();
    if (!filePath.isEmpty())
        CFDictionarySetValue(propertyList.get(), LegacyWebArchiveResourceFilePathKey, filePath.createCFString().get());

    // FrameName should be left out if empty for subresources, but always included for main resources
    auto& frameName = resource->frameName();
    if (!frameName.isEmpty() || isMainResource)
        CFDictionarySetValue(propertyList.get(), LegacyWebArchiveResourceFrameNameKey, frameName.createCFString().get());

    // Set MIMEType, TextEncodingName, and ResourceResponse only if they actually exist
    auto& mimeType = resource->mimeType();
    if (!mimeType.isEmpty())
        CFDictionarySetValue(propertyList.get(), LegacyWebArchiveResourceMIMETypeKey, mimeType.createCFString().get());

    auto& textEncoding = resource->textEncoding();
    if (!textEncoding.isEmpty())
        CFDictionarySetValue(propertyList.get(), LegacyWebArchiveResourceTextEncodingNameKey, textEncoding.createCFString().get());

    // Don't include the resource response for the main resource
    if (!isMainResource) {
        if (auto resourceResponseData = createPropertyListRepresentation(resource->response()))
            CFDictionarySetValue(propertyList.get(), LegacyWebArchiveResourceResponseKey, resourceResponseData.get());
    }

    return propertyList;
}

RetainPtr<CFDictionaryRef> LegacyWebArchive::createPropertyListRepresentation(Archive& archive)
{
    auto propertyList = adoptCF(CFDictionaryCreateMutable(0, 3, 0, &kCFTypeDictionaryValueCallBacks));

    auto mainResourceDict = createPropertyListRepresentation(archive.mainResource(), MainResource);
    ASSERT(mainResourceDict);
    if (!mainResourceDict)
        return nullptr;
    CFDictionarySetValue(propertyList.get(), LegacyWebArchiveMainResourceKey, mainResourceDict.get());

    auto subresourcesArray = adoptCF(CFArrayCreateMutable(0, archive.subresources().size(), &kCFTypeArrayCallBacks));
    for (auto& resource : archive.subresources()) {
        if (auto subresource = createPropertyListRepresentation(resource.ptr(), Subresource))
            CFArrayAppendValue(subresourcesArray.get(), subresource.get());
        else
            LOG(Archives, "LegacyWebArchive - Failed to create property list for subresource");
    }
    if (CFArrayGetCount(subresourcesArray.get()))
        CFDictionarySetValue(propertyList.get(), LegacyWebArchiveSubresourcesKey, subresourcesArray.get());

    auto subframesArray = adoptCF(CFArrayCreateMutable(0, archive.subframeArchives().size(), &kCFTypeArrayCallBacks));
    for (auto& subframe : archive.subframeArchives()) {
        if (auto subframeArchive = createPropertyListRepresentation(subframe.get()))
            CFArrayAppendValue(subframesArray.get(), subframeArchive.get());
        else
            LOG(Archives, "LegacyWebArchive - Failed to create property list for subframe archive");
    }
    if (CFArrayGetCount(subframesArray.get()))
        CFDictionarySetValue(propertyList.get(), LegacyWebArchiveSubframeArchivesKey, subframesArray.get());

    return propertyList;
}

ResourceResponse LegacyWebArchive::createResourceResponseFromPropertyListData(CFDataRef data, CFStringRef responseDataType)
{
    ASSERT(data);
    if (!data)
        return ResourceResponse();

    // If the ResourceResponseVersion (passed in as responseDataType) exists at all, this is a "new" web archive that we
    // can parse well in a cross platform manner If it doesn't exist, we will assume this is an "old" web archive with,
    // NSURLResponse objects in it and parse the ResourceResponse as such.
    if (!responseDataType)
        return createResourceResponseFromMacArchivedData(data);

    // FIXME: Parse the "new" format that the above comment references here. This format doesn't exist yet.
    return ResourceResponse();
}

RefPtr<ArchiveResource> LegacyWebArchive::createResource(CFDictionaryRef dictionary)
{
    ASSERT(dictionary);
    if (!dictionary)
        return nullptr;

    auto resourceData = static_cast<CFDataRef>(CFDictionaryGetValue(dictionary, LegacyWebArchiveResourceDataKey));
    if (resourceData && CFGetTypeID(resourceData) != CFDataGetTypeID()) {
        LOG(Archives, "LegacyWebArchive - Resource data is not of type CFData, cannot create invalid resource");
        return nullptr;
    }

    auto frameName = static_cast<CFStringRef>(CFDictionaryGetValue(dictionary, LegacyWebArchiveResourceFrameNameKey));
    if (frameName && CFGetTypeID(frameName) != CFStringGetTypeID()) {
        LOG(Archives, "LegacyWebArchive - Frame name is not of type CFString, cannot create invalid resource");
        return nullptr;
    }

    auto mimeType = static_cast<CFStringRef>(CFDictionaryGetValue(dictionary, LegacyWebArchiveResourceMIMETypeKey));
    if (mimeType && CFGetTypeID(mimeType) != CFStringGetTypeID()) {
        LOG(Archives, "LegacyWebArchive - MIME type is not of type CFString, cannot create invalid resource");
        return nullptr;
    }

    auto url = static_cast<CFStringRef>(CFDictionaryGetValue(dictionary, LegacyWebArchiveResourceURLKey));
    if (url && CFGetTypeID(url) != CFStringGetTypeID()) {
        LOG(Archives, "LegacyWebArchive - URL is not of type CFString, cannot create invalid resource");
        return nullptr;
    }

    auto textEncoding = static_cast<CFStringRef>(CFDictionaryGetValue(dictionary, LegacyWebArchiveResourceTextEncodingNameKey));
    if (textEncoding && CFGetTypeID(textEncoding) != CFStringGetTypeID()) {
        LOG(Archives, "LegacyWebArchive - Text encoding is not of type CFString, cannot create invalid resource");
        return nullptr;
    }

    ResourceResponse response;

    if (auto resourceResponseData = static_cast<CFDataRef>(CFDictionaryGetValue(dictionary, LegacyWebArchiveResourceResponseKey))) {
        if (CFGetTypeID(resourceResponseData) != CFDataGetTypeID()) {
            LOG(Archives, "LegacyWebArchive - Resource response data is not of type CFData, cannot create invalid resource");
            return nullptr;
        }

        auto resourceResponseVersion = static_cast<CFStringRef>(CFDictionaryGetValue(dictionary, LegacyWebArchiveResourceResponseVersionKey));
        if (resourceResponseVersion && CFGetTypeID(resourceResponseVersion) != CFStringGetTypeID()) {
            LOG(Archives, "LegacyWebArchive - Resource response version is not of type CFString, cannot create invalid resource");
            return nullptr;
        }

        response = createResourceResponseFromPropertyListData(resourceResponseData, resourceResponseVersion);
    }

    auto filePathValue = CFDictionaryGetValue(dictionary, LegacyWebArchiveResourceFilePathKey);
    auto filePath = dynamic_cf_cast<CFStringRef>(filePathValue);
    if (filePathValue && !filePath) {
        LOG(Archives, "LegacyWebArchive - File path is not of type CFString, cannot create invalid resource");
        return nullptr;
    }

    return ArchiveResource::create(SharedBuffer::create(resourceData), URL { url }, mimeType, textEncoding, frameName, response, filePath);
}

LegacyWebArchive::LegacyWebArchive(Vector<FrameIdentifier>&& subframeIdentifiers)
    : m_subframeIdentifiers(WTFMove(subframeIdentifiers))
{
}

Ref<LegacyWebArchive> LegacyWebArchive::create()
{
    return adoptRef(*new LegacyWebArchive);
}

Ref<LegacyWebArchive> LegacyWebArchive::create(Ref<ArchiveResource>&& mainResource, Vector<Ref<ArchiveResource>>&& subresources, Vector<FrameIdentifier>&& subframeIdentifiers)
{
    auto archive = adoptRef(*new LegacyWebArchive(WTFMove(subframeIdentifiers)));
    archive->setMainResource(WTFMove(mainResource));

    for (auto& subresource : subresources)
        archive->addSubresource(WTFMove(subresource));

    return archive;
}

Ref<LegacyWebArchive> LegacyWebArchive::create(Ref<ArchiveResource>&& mainResource, Vector<Ref<ArchiveResource>>&& subresources, Vector<Ref<LegacyWebArchive>>&& subframeArchives)
{
    auto archive = create();
    archive->setMainResource(WTFMove(mainResource));

    for (auto& subresource : subresources)
        archive->addSubresource(WTFMove(subresource));

    for (auto& subframeArchive : subframeArchives)
        archive->addSubframeArchive(WTFMove(subframeArchive));

    return archive;
}

RefPtr<LegacyWebArchive> LegacyWebArchive::create(FragmentedSharedBuffer& data)
{
    return create(URL(), data);
}

RefPtr<LegacyWebArchive> LegacyWebArchive::create(const URL&, FragmentedSharedBuffer& data)
{
    LOG(Archives, "LegacyWebArchive - Creating from raw data");

    Ref<LegacyWebArchive> archive = create();

    RetainPtr<CFDataRef> cfData = data.makeContiguous()->createCFData();
    if (!cfData)
        return nullptr;

    CFErrorRef error = nullptr;

    RetainPtr<CFDictionaryRef> plist = adoptCF(static_cast<CFDictionaryRef>(CFPropertyListCreateWithData(0, cfData.get(), kCFPropertyListImmutable, 0, &error)));
    if (!plist) {
#if !LOG_DISABLED
        RetainPtr<CFStringRef> errorString = error ? adoptCF(CFErrorCopyDescription(error)) : 0;
        const char* cError = errorString ? CFStringGetCStringPtr(errorString.get(), kCFStringEncodingUTF8) : "unknown error";
        LOG(Archives, "LegacyWebArchive - Error parsing PropertyList from archive data - %s", cError);
#endif
        if (error)
            CFRelease(error);
        return nullptr;
    }

    if (CFGetTypeID(plist.get()) != CFDictionaryGetTypeID()) {
        LOG(Archives, "LegacyWebArchive - Archive property list is not the expected CFDictionary, aborting invalid WebArchive");
        return nullptr;
    }

    if (!archive->extract(plist.get()))
        return nullptr;

    return WTFMove(archive);
}

bool LegacyWebArchive::extract(CFDictionaryRef dictionary)
{
    ASSERT(dictionary);
    if (!dictionary) {
        LOG(Archives, "LegacyWebArchive - Null root CFDictionary, aborting invalid WebArchive");
        return false;
    }

    CFDictionaryRef mainResourceDict = static_cast<CFDictionaryRef>(CFDictionaryGetValue(dictionary, LegacyWebArchiveMainResourceKey));
    if (!mainResourceDict) {
        LOG(Archives, "LegacyWebArchive - No main resource in archive, aborting invalid WebArchive");
        return false;
    }
    if (CFGetTypeID(mainResourceDict) != CFDictionaryGetTypeID()) {
        LOG(Archives, "LegacyWebArchive - Main resource is not the expected CFDictionary, aborting invalid WebArchive");
        return false;
    }

    auto mainResource = createResource(mainResourceDict);
    if (!mainResource) {
        LOG(Archives, "LegacyWebArchive - Failed to parse main resource from CFDictionary or main resource does not exist, aborting invalid WebArchive");
        return false;
    }

    if (mainResource->mimeType().isNull()) {
        LOG(Archives, "LegacyWebArchive - Main resource MIME type is required, but was null.");
        return false;
    }

    setMainResource(mainResource.releaseNonNull());

    auto subresourceArray = static_cast<CFArrayRef>(CFDictionaryGetValue(dictionary, LegacyWebArchiveSubresourcesKey));
    if (subresourceArray && CFGetTypeID(subresourceArray) != CFArrayGetTypeID()) {
        LOG(Archives, "LegacyWebArchive - Subresources is not the expected Array, aborting invalid WebArchive");
        return false;
    }

    if (subresourceArray) {
        auto count = CFArrayGetCount(subresourceArray);
        for (CFIndex i = 0; i < count; ++i) {
            auto subresourceDict = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(subresourceArray, i));
            if (CFGetTypeID(subresourceDict) != CFDictionaryGetTypeID()) {
                LOG(Archives, "LegacyWebArchive - Subresource is not expected CFDictionary, aborting invalid WebArchive");
                return false;
            }

            if (auto subresource = createResource(subresourceDict))
                addSubresource(subresource.releaseNonNull());
        }
    }

    auto subframeArray = static_cast<CFArrayRef>(CFDictionaryGetValue(dictionary, LegacyWebArchiveSubframeArchivesKey));
    if (subframeArray && CFGetTypeID(subframeArray) != CFArrayGetTypeID()) {
        LOG(Archives, "LegacyWebArchive - Subframe archives is not the expected Array, aborting invalid WebArchive");
        return false;
    }

    if (subframeArray) {
        auto count = CFArrayGetCount(subframeArray);
        for (CFIndex i = 0; i < count; ++i) {
            auto subframeDict = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(subframeArray, i));
            if (CFGetTypeID(subframeDict) != CFDictionaryGetTypeID()) {
                LOG(Archives, "LegacyWebArchive - Subframe array is not expected CFDictionary, aborting invalid WebArchive");
                return false;
            }

            auto subframeArchive = create();
            if (subframeArchive->extract(subframeDict))
                addSubframeArchive(WTFMove(subframeArchive));
            else
                LOG(Archives, "LegacyWebArchive - Invalid subframe archive skipped");
        }
    }

    return true;
}

RetainPtr<CFDataRef> LegacyWebArchive::rawDataRepresentation()
{
    auto propertyList = createPropertyListRepresentation(*this);
    ASSERT(propertyList);
    if (!propertyList) {
        LOG(Archives, "LegacyWebArchive - Failed to create property list for archive, returning no data");
        return nullptr;
    }

    auto stream = adoptCF(CFWriteStreamCreateWithAllocatedBuffers(0, 0));

    CFWriteStreamOpen(stream.get());
    CFPropertyListWrite(propertyList.get(), stream.get(), kCFPropertyListBinaryFormat_v1_0, 0, 0);

    auto plistData = adoptCF(static_cast<CFDataRef>(CFWriteStreamCopyProperty(stream.get(), kCFStreamPropertyDataWritten)));
    ASSERT(plistData);

    CFWriteStreamClose(stream.get());

    if (!plistData) {
        LOG(Archives, "LegacyWebArchive - Failed to convert property list into raw data, returning no data");
        return nullptr;
    }

    return plistData;
}

#if !PLATFORM(COCOA)

ResourceResponse LegacyWebArchive::createResourceResponseFromMacArchivedData(CFDataRef responseData)
{
    // FIXME: If is is possible to parse in a serialized NSURLResponse manually, without using
    // NSKeyedUnarchiver, manipulating plists directly, then we want to do that here.
    // Until then, this can be done on Mac only.
    return ResourceResponse();
}

RetainPtr<CFDataRef> LegacyWebArchive::createPropertyListRepresentation(const ResourceResponse& response)
{
    // FIXME: Write out the "new" format described in createResourceResponseFromPropertyListData once we invent it.
    return nullptr;
}

#endif

RefPtr<LegacyWebArchive> LegacyWebArchive::create(Node& node)
{
    return create(node, { });
}

RefPtr<LegacyWebArchive> LegacyWebArchive::create(Node& node, ArchiveOptions&& options, NOESCAPE const Function<bool(LocalFrame&)>& frameFilter)
{
    RefPtr frame = node.document().frame();
    if (!frame)
        return create();

    auto currentOptions = WTFMove(options);
    // If the page was loaded with JavaScript enabled, we don't want to archive <noscript> tags
    // In practice we don't actually know whether scripting was enabled when the page was originally loaded
    // but we can approximate that by checking if scripting is enabled right now.
    if (frame->page() && frame->page()->settings().isScriptEnabled())
        currentOptions.markupExclusionRules.append(MarkupExclusionRule { AtomString { "noscript"_s }, { } });

    // This archive is created for saving, and all subresources URLs will be rewritten to relative file paths
    // based on the main resource file.
    if (!currentOptions.mainResourceFileName.isEmpty())
        currentOptions.markupExclusionRules.append(MarkupExclusionRule { AtomString { "base"_s }, { } });

    return createInternal(node, currentOptions, frameFilter);
}

RefPtr<LegacyWebArchive> LegacyWebArchive::create(LocalFrame& frame)
{
    return create(frame, { });
}

RefPtr<LegacyWebArchive> LegacyWebArchive::create(LocalFrame& frame, ArchiveOptions&& options)
{
    RefPtr documentLoader = frame.loader().documentLoader();
    if (!documentLoader)
        return nullptr;

    auto mainResource = documentLoader->mainResource();
    if (!mainResource)
        return nullptr;

    Vector<Ref<LegacyWebArchive>> subframeArchives;
    Vector<FrameIdentifier> subframeIdentifiers;
    for (RefPtr child = frame.tree().firstChild(); child; child = child->tree().nextSibling()) {
        if (options.shouldArchiveSubframes == ShouldArchiveSubframes::No) {
            subframeIdentifiers.append(child->frameID());
            continue;
        }

        if (auto localChild = dynamicDowncast<LocalFrame>(child.get())) {
            if (auto childFrameArchive = create(*localChild, { }))
                subframeArchives.append(childFrameArchive.releaseNonNull());
        }
    }

    if (!subframeIdentifiers.isEmpty()) {
        ASSERT(subframeArchives.isEmpty());
        return create(mainResource.releaseNonNull(), documentLoader->subresources(), WTFMove(subframeIdentifiers));
    }

    return create(mainResource.releaseNonNull(), documentLoader->subresources(), WTFMove(subframeArchives));
}

RefPtr<LegacyWebArchive> LegacyWebArchive::create(const SimpleRange& range)
{
    return LegacyWebArchive::create(range, { });
}

RefPtr<LegacyWebArchive> LegacyWebArchive::create(const SimpleRange& range, ArchiveOptions&& options)
{
    Ref document = range.start.document();
    RefPtr frame = document->frame();
    if (!frame)
        return nullptr;

    // FIXME: This is always "for interchange". Is that right?
    Vector<Ref<Node>> nodeList;
    auto markupString = makeString(documentTypeString(document), serializePreservingVisualAppearance(range, &nodeList, AnnotateForInterchange::Yes));
    return createInternal(markupString, WTFMove(options), *frame, WTFMove(nodeList), nullptr);
}

#if ENABLE(ATTACHMENT_ELEMENT)

static void addSubresourcesForAttachmentElementsIfNecessary(LocalFrame& frame, const Vector<Ref<Node>>& nodes, Vector<Ref<ArchiveResource>>& subresources)
{
    if (!DeprecatedGlobalSettings::attachmentElementEnabled())
        return;

    Vector<String> identifiers;
    for (Ref node : nodes) {
        RefPtr attachment = dynamicDowncast<HTMLAttachmentElement>(node);
        if (!attachment)
            continue;

        auto uniqueIdentifier = attachment->uniqueIdentifier();
        if (uniqueIdentifier.isEmpty())
            continue;

        identifiers.append(WTFMove(uniqueIdentifier));
    }

    if (identifiers.isEmpty())
        return;

    auto* editorClient = frame.editor().client();
    if (!editorClient)
        return;

    auto frameName = frame.tree().uniqueName();
    for (auto& data : editorClient->serializedAttachmentDataForIdentifiers(WTFMove(identifiers))) {
        auto resourceURL = HTMLAttachmentElement::archiveResourceURL(data.identifier);
        if (auto resource = ArchiveResource::create(data.data.ptr(), WTFMove(resourceURL), data.mimeType, { }, frameName))
            subresources.append(resource.releaseNonNull());
    }
}

#endif

static HashMap<Ref<CSSStyleSheet>, String> addSubresourcesForCSSStyleSheetsIfNecessary(LocalFrame& frame, const String& subresourcesDirectoryName, HashSet<String>& uniqueFileNames, HashMap<String, String>& uniqueSubresources, Vector<Ref<ArchiveResource>>& subresources)
{
    if (subresourcesDirectoryName.isEmpty())
        return { };

    RefPtr document = frame.document();
    if (!document)
        return { };

    CSS::SerializationContext serializationContext;

    HashMap<Ref<CSSStyleSheet>, String> uniqueCSSStyleSheets;
    Ref documentStyleSheets = document->styleSheets();
    for (unsigned index = 0; index < documentStyleSheets->length(); ++index) {
        RefPtr cssStyleSheet = dynamicDowncast<CSSStyleSheet>(documentStyleSheets->item(index));
        if (!cssStyleSheet)
            continue;

        if (uniqueCSSStyleSheets.contains(*cssStyleSheet))
            continue;

        HashSet<RefPtr<CSSStyleSheet>> cssStyleSheets;
        cssStyleSheets.add(cssStyleSheet.get());
        cssStyleSheet->getChildStyleSheets(cssStyleSheets);
        for (auto& currentCSSStyleSheet : cssStyleSheets) {
            bool isExternalStyleSheet = !currentCSSStyleSheet->href().isEmpty() || currentCSSStyleSheet->ownerRule();
            if (!isExternalStyleSheet)
                continue;

            auto url = currentCSSStyleSheet->baseURL();
            if (url.isNull() || url.isEmpty())
                continue;

            auto addResult = uniqueCSSStyleSheets.add(*currentCSSStyleSheet, emptyString());
            if (!addResult.isNewEntry)
                continue;

            // Delete cached resource for this style sheet.
            auto index = subresources.findIf([&](auto& subresource) {
                return subresource->url() == url;
            });
            if (index != notFound) {
                auto fileName = FileSystem::lastComponentOfPathIgnoringTrailingSlash(subresources[index]->relativeFilePath());
                uniqueFileNames.remove(fileName);
                uniqueSubresources.remove(url.string());
                subresources.removeAt(index);
            }

            auto extension = MIMETypeRegistry::preferredExtensionForMIMEType(cssContentTypeAtom());
            String subresourceFileName = generateValidFileName(url, uniqueFileNames, extension);
            uniqueFileNames.add(subresourceFileName);
            addResult.iterator->value = FileSystem::pathByAppendingComponent(subresourcesDirectoryName, subresourceFileName);
            serializationContext.replacementURLStringsForCSSStyleSheet.add(*currentCSSStyleSheet, subresourceFileName);
        }
    }

    auto frameName = frame.tree().uniqueName();
    for (auto& [urlString, path] : uniqueSubresources) {
        // The style sheet files are stored in the same directory as other subresources.
        serializationContext.replacementURLStrings.add(urlString, FileSystem::lastComponentOfPathIgnoringTrailingSlash(path));
    }

    for (auto& [cssStyleSheet, path]  : uniqueCSSStyleSheets) {
        auto contentString = cssStyleSheet->cssText(serializationContext);
        if (auto newResource = ArchiveResource::create(utf8Buffer(contentString), URL { cssStyleSheet->href() }, "text/css"_s, "UTF-8"_s, frameName, ResourceResponse(), path))
            subresources.append(newResource.releaseNonNull());
    }

    return frame.isMainFrame() ? uniqueCSSStyleSheets : serializationContext.replacementURLStringsForCSSStyleSheet;
}

RefPtr<LegacyWebArchive> LegacyWebArchive::createInternal(Node& node, const ArchiveOptions& options, NOESCAPE const Function<bool(LocalFrame&)>& frameFilter)
{
    RefPtr frame = node.document().frame();
    if (!frame)
        return create();

    Vector<Ref<Node>> nodeList;
    String markupString = serializeFragment(node, SerializedNodes::SubtreeIncludingNode, &nodeList, ResolveURLs::No, std::nullopt, SerializeShadowRoots::AllForInterchange, { }, options.markupExclusionRules);
    auto nodeType = node.nodeType();
    if (nodeType != Node::DOCUMENT_NODE && nodeType != Node::DOCUMENT_TYPE_NODE)
        markupString = makeString(documentTypeString(node.document()), markupString);

    return createInternal(markupString, options, *frame, WTFMove(nodeList), frameFilter);
}

RefPtr<LegacyWebArchive> LegacyWebArchive::createInternal(const String& markupString, const ArchiveOptions& options, LocalFrame& frame, Vector<Ref<Node>>&& nodes, NOESCAPE const Function<bool(LocalFrame&)>& frameFilter)
{
    auto& response = frame.loader().documentLoader()->response();
    URL responseURL = response.url();

    // it's possible to have a response without a URL here
    // <rdar://problem/5454935>
    if (responseURL.isNull())
        responseURL = URL({ }, emptyString());

    auto mainResource = ArchiveResource::create(utf8Buffer(markupString), responseURL, response.mimeType(), "UTF-8"_s, frame.tree().uniqueName());
    if (!mainResource)
        return nullptr;

    Vector<Ref<LegacyWebArchive>> subframeArchives;
    Vector<FrameIdentifier> subframeIdentifiers;
    Vector<Ref<ArchiveResource>> subresources;
    HashMap<String, String> uniqueSubresources;
    HashSet<String> uniqueFileNames;
    String subresourcesDirectoryName = options.mainResourceFileName.isNull() ? String { } : makeString(options.mainResourceFileName, "_files"_s);

    for (auto& node : nodes) {
        RefPtr frameOwnerElement = dynamicDowncast<HTMLFrameOwnerElement>(node);
        RefPtr childFrame = frameOwnerElement ? frameOwnerElement->contentFrame() : nullptr;
        RefPtr localChildFrame = dynamicDowncast<LocalFrame>(childFrame.get());
        if (childFrame) {
            if (frameFilter && localChildFrame && !frameFilter(*localChildFrame))
                continue;

            if (options.shouldArchiveSubframes == ShouldArchiveSubframes::No) {
                subframeIdentifiers.append(childFrame->frameID());
                continue;
            }

            if (!localChildFrame)
                continue;

            if (auto subframeArchive = createInternal(*localChildFrame->document(), options, frameFilter)) {
                RefPtr subframeMainResource = subframeArchive->mainResource();
                auto subframeMainResourceURL = subframeMainResource ? subframeMainResource->url() : URL { };
                if (!subframeMainResourceURL.isEmpty()) {
                    auto subframeMainResourceRelativePath = frame.isMainFrame() ? subframeMainResource->relativeFilePath() : FileSystem::lastComponentOfPathIgnoringTrailingSlash(subframeMainResource->relativeFilePath());
                    uniqueSubresources.add(makeString(childFrame->frameID().toUInt64()), subframeMainResourceRelativePath);
                }
                subframeArchives.append(subframeArchive.releaseNonNull());
            } else
                LOG_ERROR("Unabled to archive subframe %s", childFrame->tree().uniqueName().string().utf8().data());

        } else {
            ListHashSet<URL> subresourceURLs;
            node->getSubresourceURLs(subresourceURLs);
            node->getCandidateSubresourceURLs(subresourceURLs);

            if (options.shouldSaveScriptsFromMemoryCache == ShouldSaveScriptsFromMemoryCache::Yes && responseURL.protocolIsInHTTPFamily()) {
                RegistrableDomain domain { responseURL };
                MemoryCache::singleton().forEachSessionResource(frame.page()->sessionID(), [&](auto& resource) {
                    if (domain.matches(resource.url()) && resource.hasClients() && resource.type() == CachedResource::Type::Script)
                        subresourceURLs.add(resource.url());
                });
            }

            ASSERT(frame.loader().documentLoader());
            Ref documentLoader = *frame.loader().documentLoader();

            for (auto& subresourceURL : subresourceURLs) {
                if (uniqueSubresources.contains(subresourceURL.string()))
                    continue;

                // WebArchive is created for saving, and we don't need to store resources for data URLs.
                if (!subresourcesDirectoryName.isNull() && subresourceURL.protocolIsData())
                    continue;

                auto addResult = uniqueSubresources.add(subresourceURL.string(), emptyString());
                auto resource = documentLoader->subresource(subresourceURL);
                if (!resource) {
                    ResourceRequest request(URL { subresourceURL });
                    request.setDomainForCachePartition(frame.document()->domainForCachePartition());
                    if (auto* cachedResource = MemoryCache::singleton().resourceForRequest(request, frame.page()->sessionID()))
                        resource = ArchiveResource::create(cachedResource->resourceBuffer(), subresourceURL, cachedResource->response());
                }

                if (!resource) {
                    // FIXME: should do something better than spew to console here
                    LOG_ERROR("Failed to archive subresource for %s", subresourceURL.string().utf8().data());
                    continue;
                }

                if (!subresourcesDirectoryName.isNull()) {
                    String subresourceFileName = generateValidFileName(subresourceURL, uniqueFileNames);
                    uniqueFileNames.add(subresourceFileName);
                    String subresourceFilePath = FileSystem::pathByAppendingComponent(subresourcesDirectoryName, subresourceFileName);
                    resource->setRelativeFilePath(subresourceFilePath);
                    addResult.iterator->value = frame.isMainFrame() ? subresourceFilePath : subresourceFileName;
                }

                subresources.append(resource.releaseNonNull());
            }
        }
    }

    auto uniqueCSSStyleSheets = addSubresourcesForCSSStyleSheetsIfNecessary(frame, subresourcesDirectoryName, uniqueFileNames, uniqueSubresources, subresources);

#if ENABLE(ATTACHMENT_ELEMENT)
    addSubresourcesForAttachmentElementsIfNecessary(frame, nodes, subresources);
#endif

    // If we are archiving the entire page, add any link icons that we have data for.
    if (!nodes.isEmpty() && nodes[0]->isDocumentNode()) {
        RefPtr documentLoader = frame.loader().documentLoader();
        ASSERT(documentLoader);
        for (auto& icon : documentLoader->linkIcons()) {
            if (auto resource = documentLoader->subresource(icon.url))
                subresources.append(resource.releaseNonNull());
        }
    }

    if (!options.mainResourceFileName.isNull()) {
        RefPtr document = frame.document();
        if (!document)
            return nullptr;

        if (responseURL.isEmpty())
            return nullptr;

        auto extension = MIMETypeRegistry::preferredExtensionForMIMEType(textHTMLContentTypeAtom());
        if (!extension.isEmpty())
            extension = makeString('.', extension);
        auto mainFrameFileNameWithExtension = options.mainResourceFileName.endsWith(extension) ? options.mainResourceFileName : makeString(options.mainResourceFileName, extension);
        auto fileNameWithExtension = frame.isMainFrame() ? mainFrameFileNameWithExtension : makeString(subresourcesDirectoryName, "/frame_"_s, frame.frameID().toUInt64(), extension);

        ResolveURLs resolveURLs = ResolveURLs::No;
        // Base element is excluded, so all URLs should be replaced with absolute URL.
        bool baseElementExcluded = std::ranges::any_of(options.markupExclusionRules, [&](auto& rule) {
            return rule.elementLocalName == "base"_s;
        });
        if (!document->baseElementURL().isEmpty() && baseElementExcluded)
            resolveURLs = ResolveURLs::Yes;

        String updatedMarkupString = serializeFragmentWithURLReplacement(*document, SerializedNodes::SubtreeIncludingNode, nullptr, resolveURLs, std::nullopt, WTFMove(uniqueSubresources), WTFMove(uniqueCSSStyleSheets), SerializeShadowRoots::AllForInterchange, { }, options.markupExclusionRules);
        mainResource = ArchiveResource::create(utf8Buffer(updatedMarkupString), responseURL, response.mimeType(), "UTF-8"_s, frame.tree().uniqueName(), ResourceResponse(), fileNameWithExtension);
    }

    if (!subframeIdentifiers.isEmpty()) {
        ASSERT(subframeArchives.isEmpty());
        return create(mainResource.releaseNonNull(), WTFMove(subresources), WTFMove(subframeIdentifiers));
    }

    return create(mainResource.releaseNonNull(), WTFMove(subresources), WTFMove(subframeArchives));
}

RefPtr<LegacyWebArchive> LegacyWebArchive::createFromSelection(LocalFrame* frame)
{
    return createFromSelection(frame, { });
}

RefPtr<LegacyWebArchive> LegacyWebArchive::createFromSelection(LocalFrame* frame, ArchiveOptions&& options)
{
    if (!frame)
        return nullptr;

    RefPtr document = frame->document();
    if (!document)
        return nullptr;

    StringBuilder builder;
    builder.append(documentTypeString(*document));

    Vector<Ref<Node>> nodeList;
    builder.append(serializePreservingVisualAppearance(frame->selection().selection(), ResolveURLs::No, SerializeComposedTree::Yes, IgnoreUserSelectNone::Yes, PreserveBaseElement::Yes, PreserveDirectionForInlineText::Yes, &nodeList));

    RefPtr archive = createInternal(builder.toString(), WTFMove(options), *frame, WTFMove(nodeList), { });
    if (!archive)
        return nullptr;

    if (!document->isFrameSet())
        return archive;

    // Wrap the frameset document in an iframe so it can be pasted into
    // another document (which will have a body or frameset of its own). 
    auto iframeMarkup = makeString("<iframe frameborder=\"no\" marginwidth=\"0\" marginheight=\"0\" width=\"98%%\" height=\"98%%\" src=\""_s, frame->loader().documentLoader()->response().url().string(), "\"></iframe>"_s);
    auto iframeResource = ArchiveResource::create(utf8Buffer(iframeMarkup), aboutBlankURL(), textHTMLContentTypeAtom(), "UTF-8"_s, String());

    return create(iframeResource.releaseNonNull(), { }, { archive.releaseNonNull() });
}

}
