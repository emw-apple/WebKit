/*
 *  Copyright (C) 2007 Holger Hans Peter Freyther
 *  Copyright (C) 2007 Alp Toker <alp@atoker.com>
 *  Copyright (C) 2014-2015, 2025 Igalia S.L.
 *  Copyright (C) 2025 Microsoft Corporation. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "Pasteboard.h"

#include "Color.h"
#include "CommonAtomStrings.h"
#include "DragData.h"
#include "Image.h"
#include "MIMETypeRegistry.h"
#include "NotImplemented.h"
#include "PasteboardStrategy.h"
#include "PlatformStrategies.h"
#include "SelectionData.h"
#include "SharedBuffer.h"
#include <wtf/URL.h>

namespace WebCore {

enum ClipboardDataType {
    ClipboardDataTypeText,
    ClipboardDataTypeMarkup,
    ClipboardDataTypeURIList,
    ClipboardDataTypeURL,
    ClipboardDataTypeImage,
    ClipboardDataTypeUnknown
};

static ASCIILiteral pasteboardCustomDataType()
{
#if PLATFORM(GTK)
    return PasteboardCustomData::gtkType();
#elif PLATFORM(WPE)
    return PasteboardCustomData::wpeType();
#endif
}

std::unique_ptr<Pasteboard> Pasteboard::createForCopyAndPaste(std::unique_ptr<PasteboardContext>&& context)
{
    return makeUnique<Pasteboard>(WTFMove(context), "CLIPBOARD"_s);
}

#if ENABLE(DRAG_SUPPORT)
std::unique_ptr<Pasteboard> Pasteboard::createForDragAndDrop(std::unique_ptr<PasteboardContext>&& context)
{
    return makeUnique<Pasteboard>(WTFMove(context), SelectionData());
}

std::unique_ptr<Pasteboard> Pasteboard::create(const DragData& dragData)
{
    ASSERT(dragData.platformData());
    return makeUnique<Pasteboard>(dragData.createPasteboardContext(), *dragData.platformData());
}

Pasteboard::Pasteboard(std::unique_ptr<PasteboardContext>&& context, SelectionData&& selectionData)
    : m_context(WTFMove(context))
    , m_selectionData(WTFMove(selectionData))
{
}

Pasteboard::Pasteboard(std::unique_ptr<PasteboardContext>&& context, SelectionData& selectionData)
    : m_context(WTFMove(context))
    , m_selectionData(selectionData)
{
}
#endif

Pasteboard::Pasteboard(std::unique_ptr<PasteboardContext>&& context, const String& name)
    : m_context(WTFMove(context))
    , m_name(name)
    , m_changeCount(platformStrategies()->pasteboardStrategy()->changeCount(m_name))
{
}

Pasteboard::Pasteboard(std::unique_ptr<PasteboardContext>&& context)
    : m_context(WTFMove(context))
{
}

Pasteboard::~Pasteboard() = default;

#if ENABLE(DRAG_SUPPORT)
const SelectionData& Pasteboard::selectionData() const
{
    ASSERT(m_selectionData);
    return *m_selectionData;
}
#endif

static ClipboardDataType selectionDataTypeFromHTMLClipboardType(const String& type)
{
    // From the Mac port: Ignore any trailing charset - JS strings are
    // Unicode, which encapsulates the charset issue.
    if (type == textPlainContentTypeAtom())
        return ClipboardDataTypeText;
    if (type == textHTMLContentTypeAtom())
        return ClipboardDataTypeMarkup;
    if (type == "Files"_s || type == "text/uri-list"_s)
        return ClipboardDataTypeURIList;

    // Not a known type, so just default to using the text portion.
    return ClipboardDataTypeUnknown;
}

void Pasteboard::writeString(const String& type, const String& data)
{
    ASSERT(m_selectionData);
    switch (selectionDataTypeFromHTMLClipboardType(type)) {
    case ClipboardDataTypeURIList:
    case ClipboardDataTypeURL:
        m_selectionData->setURIList(data);
        break;
    case ClipboardDataTypeMarkup:
        m_selectionData->setMarkup(data);
        break;
    case ClipboardDataTypeText:
        m_selectionData->setText(data);
        break;
    case ClipboardDataTypeUnknown:
    case ClipboardDataTypeImage:
        break;
    }
}

void Pasteboard::writePlainText(const String& text, SmartReplaceOption smartReplaceOption)
{
    if (m_selectionData) {
        m_selectionData->clearAll();
        m_selectionData->setText(text);
        m_selectionData->setCanSmartReplace(smartReplaceOption == CanSmartReplace);
    } else {
        SelectionData data;
        data.setText(text);
#if PLATFORM(GTK)
        data.setCanSmartReplace(smartReplaceOption == CanSmartReplace);
#endif
        platformStrategies()->pasteboardStrategy()->writeToClipboard(m_name, WTFMove(data));
    }
}

void Pasteboard::write(const PasteboardURL& pasteboardURL)
{
    ASSERT(!pasteboardURL.url.isEmpty());
    if (m_selectionData) {
        m_selectionData->clearAll();
        m_selectionData->setURL(pasteboardURL.url, pasteboardURL.title);
    } else {
        SelectionData data;
        data.setURL(pasteboardURL.url, pasteboardURL.title);
        platformStrategies()->pasteboardStrategy()->writeToClipboard(m_name, WTFMove(data));
    }
}

void Pasteboard::writeTrustworthyWebURLsPboardType(const PasteboardURL&)
{
    notImplemented();
}

void Pasteboard::write(const PasteboardImage& pasteboardImage)
{
    if (m_selectionData) {
        m_selectionData->clearAll();
        if (!pasteboardImage.url.url.isEmpty()) {
            m_selectionData->setURL(pasteboardImage.url.url, pasteboardImage.url.title);
            m_selectionData->setMarkup(pasteboardImage.url.markup);
        }
        m_selectionData->setImage(pasteboardImage.image.get());
    } else {
        SelectionData data;
        if (!pasteboardImage.url.url.isEmpty()) {
            data.setURL(pasteboardImage.url.url, pasteboardImage.url.title);
            data.setMarkup(pasteboardImage.url.markup);
        }
        data.setImage(pasteboardImage.image.get());
        platformStrategies()->pasteboardStrategy()->writeToClipboard(m_name, WTFMove(data));
    }
}

void Pasteboard::write(const PasteboardBuffer&)
{
}

void Pasteboard::write(const PasteboardWebContent& pasteboardContent)
{
    if (m_selectionData) {
        m_selectionData->clearAll();
        m_selectionData->setText(pasteboardContent.text);
        m_selectionData->setMarkup(pasteboardContent.markup);
        m_selectionData->setCanSmartReplace(pasteboardContent.canSmartCopyOrDelete);
        PasteboardCustomData customData;
        customData.setOrigin(pasteboardContent.contentOrigin);
        m_selectionData->setCustomData(customData.createSharedBuffer());
    } else {
        SelectionData data;
        data.setText(pasteboardContent.text);
        data.setMarkup(pasteboardContent.markup);
#if PLATFORM(GTK)
        data.setCanSmartReplace(pasteboardContent.canSmartCopyOrDelete);
#endif
        PasteboardCustomData customData;
        customData.setOrigin(pasteboardContent.contentOrigin);
        data.setCustomData(customData.createSharedBuffer());
        platformStrategies()->pasteboardStrategy()->writeToClipboard(m_name, WTFMove(data));
    }
}

void Pasteboard::clear()
{
    if (!m_selectionData) {
        platformStrategies()->pasteboardStrategy()->clearClipboard(m_name);
        return;
    }

    // We do not clear filenames. According to the spec: "The clearData() method
    // does not affect whether any files were included in the drag, so the types
    // attribute's list might still not be empty after calling clearData() (it would
    // still contain the "Files" string if any files were included in the drag)."
    m_selectionData->clearAllExceptFilenames();
}

void Pasteboard::clear(const String& type)
{
    ASSERT(m_selectionData);
    switch (selectionDataTypeFromHTMLClipboardType(type)) {
    case ClipboardDataTypeURIList:
    case ClipboardDataTypeURL:
        m_selectionData->clearURIList();
        break;
    case ClipboardDataTypeMarkup:
        m_selectionData->clearMarkup();
        break;
    case ClipboardDataTypeText:
        m_selectionData->clearText();
        break;
    case ClipboardDataTypeImage:
        m_selectionData->clearImage();
        break;
    case ClipboardDataTypeUnknown:
        m_selectionData->clearAll();
        break;
    }
}

#if ENABLE(DRAG_SUPPORT)
void Pasteboard::setDragImage(DragImage, const IntPoint&)
{
}
#endif

void Pasteboard::read(PasteboardWebContentReader& reader, WebContentReadingPolicy policy, std::optional<size_t>)
{
    reader.setContentOrigin(readOrigin());

    if (m_selectionData) {
        if (m_selectionData->hasMarkup() && reader.readHTML(m_selectionData->markup()))
            return;

        if (policy == WebContentReadingPolicy::OnlyRichTextTypes)
            return;

        if (m_selectionData->hasFilenames() && reader.readFilePaths(m_selectionData->filenames()))
            return;

        if (m_selectionData->hasText() && reader.readPlainText(m_selectionData->text()))
            return;

        return;
    }

    auto types = platformStrategies()->pasteboardStrategy()->types(m_name);
    if (types.contains("text/html"_s)) {
        auto text = platformStrategies()->pasteboardStrategy()->readTextFromClipboard(m_name, "text/html"_s);
        if (!text.isNull() && reader.readHTML(text))
            return;
    }

    if (policy == WebContentReadingPolicy::OnlyRichTextTypes)
        return;

    static const ASCIILiteral imageTypes[] = { "image/png"_s, "image/jpeg"_s, "image/gif"_s, "image/bmp"_s, "image/vnd.microsoft.icon"_s, "image/x-icon"_s };
    for (const auto& imageType : imageTypes) {
        if (types.contains(imageType)) {
            auto buffer = platformStrategies()->pasteboardStrategy()->readBufferFromClipboard(m_name, imageType);
            if (!buffer->isEmpty() && reader.readImage(buffer.releaseNonNull(), imageType))
                return;
        }
    }

    if (types.contains("text/uri-list"_s)) {
        auto filePaths = platformStrategies()->pasteboardStrategy()->readFilePathsFromClipboard(m_name);
        if (reader.readFilePaths(filePaths))
            return;
    }

    if (types.contains("text/plain"_s)) {
        auto text = platformStrategies()->pasteboardStrategy()->readTextFromClipboard(m_name, "text/plain"_s);
        if (!text.isNull() && reader.readPlainText(text))
            return;
    }

    if (types.contains("text/plain;charset=utf-8"_s)) {
        auto text = platformStrategies()->pasteboardStrategy()->readTextFromClipboard(m_name, "text/plain;charset=utf-8"_s);
        if (!text.isNull() && reader.readPlainText(text))
            return;
    }
}

void Pasteboard::read(PasteboardFileReader& reader, std::optional<size_t> index)
{
    if (m_selectionData) {
        for (const auto& filePath : m_selectionData->filenames())
            reader.readFilename(filePath);
        return;
    }

    if (!index) {
        auto filePaths = platformStrategies()->pasteboardStrategy()->readFilePathsFromClipboard(m_name);
        for (const auto& filePath : filePaths)
            reader.readFilename(filePath);
        return;
    }

    if (reader.shouldReadBuffer("image/png"_s)) {
        if (auto buffer = readBuffer(index, "image/png"_s))
            reader.readBuffer({ }, { }, buffer.releaseNonNull());
    }
}

bool Pasteboard::hasData()
{
    if (m_selectionData)
        return m_selectionData->hasText() || m_selectionData->hasMarkup() || m_selectionData->hasURIList() || m_selectionData->hasImage() || m_selectionData->hasCustomData();
    return !platformStrategies()->pasteboardStrategy()->types(m_name).isEmpty();
}

Vector<String> Pasteboard::typesSafeForBindings(const String& origin)
{
    if (m_selectionData) {
        ListHashSet<String> types;
        if (auto& buffer = m_selectionData->customData()) {
            auto customData = PasteboardCustomData::fromSharedBuffer(*buffer);
            if (customData.origin() == origin) {
                for (auto& type : customData.orderedTypes())
                    types.add(type);
            }
        }

        if (m_selectionData->hasText())
            types.add(textPlainContentTypeAtom());
        if (m_selectionData->hasMarkup())
            types.add(textHTMLContentTypeAtom());
        if (m_selectionData->hasURIList())
            types.add("text/uri-list"_s);

        return copyToVector(types);
    }

    return platformStrategies()->pasteboardStrategy()->typesSafeForDOMToReadAndWrite(m_name, origin, context());
}

Vector<String> Pasteboard::typesForLegacyUnsafeBindings()
{
    if (!m_selectionData)
        return platformStrategies()->pasteboardStrategy()->types(m_name);

    Vector<String> types;
    if (m_selectionData->hasText()) {
        types.append(textPlainContentTypeAtom());
        types.append("Text"_s);
        types.append("text"_s);
    }

    if (m_selectionData->hasMarkup())
        types.append(textHTMLContentTypeAtom());

    if (m_selectionData->hasURIList()) {
        types.append("text/uri-list"_s);
        types.append("URL"_s);
    }

    return types;
}

String Pasteboard::readOrigin()
{
    if (m_selectionData) {
        if (auto& buffer = m_selectionData->customData())
            return PasteboardCustomData::fromSharedBuffer(*buffer).origin();

        return { };
    }

    // FIXME: cache custom data?
    if (auto buffer = platformStrategies()->pasteboardStrategy()->readBufferFromClipboard(m_name, pasteboardCustomDataType()))
        return PasteboardCustomData::fromSharedBuffer(*buffer).origin();

    return { };
}

String Pasteboard::readString(const String& type)
{
    if (!m_selectionData)
        return platformStrategies()->pasteboardStrategy()->readTextFromClipboard(m_name, type);

    switch (selectionDataTypeFromHTMLClipboardType(type)) {
    case ClipboardDataTypeURIList:
        return m_selectionData->uriList();
    case ClipboardDataTypeURL:
        return m_selectionData->url().string();
    case ClipboardDataTypeMarkup:
        return m_selectionData->markup();
    case ClipboardDataTypeText:
        return m_selectionData->text();
    case ClipboardDataTypeUnknown:
    case ClipboardDataTypeImage:
        break;
    }

    return { };
}

String Pasteboard::readStringInCustomData(const String& type)
{
    if (m_selectionData) {
        if (auto& buffer = m_selectionData->customData())
            return PasteboardCustomData::fromSharedBuffer(*buffer).readStringInCustomData(type);

        return { };
    }

    // FIXME: cache custom data?
    if (auto buffer = platformStrategies()->pasteboardStrategy()->readBufferFromClipboard(m_name, pasteboardCustomDataType()))
        return PasteboardCustomData::fromSharedBuffer(*buffer).readStringInCustomData(type);

    return { };
}

Pasteboard::FileContentState Pasteboard::fileContentState()
{
    if (m_selectionData)
        return m_selectionData->filenames().isEmpty() ? FileContentState::NoFileOrImageData : FileContentState::MayContainFilePaths;

    auto types = platformStrategies()->pasteboardStrategy()->types(m_name);
    if (types.contains("text/uri-list"_s)) {
        auto filePaths = platformStrategies()->pasteboardStrategy()->readFilePathsFromClipboard(m_name);
        if (!filePaths.isEmpty())
            return FileContentState::MayContainFilePaths;
    }

    auto result = types.findIf([](const String& type) {
        return MIMETypeRegistry::isSupportedImageMIMEType(type);
    });
    return result == notFound ? FileContentState::NoFileOrImageData : FileContentState::MayContainFilePaths;
}

void Pasteboard::writeMarkup(const String&)
{
}

void Pasteboard::writeCustomData(const Vector<PasteboardCustomData>& data)
{
    if (m_selectionData) {
        if (!data.isEmpty()) {
            const auto& customData = data[0];
            customData.forEachPlatformString([this] (auto& type, auto& string) {
                writeString(type, string);
            });
            if (customData.hasSameOriginCustomData() || !customData.origin().isEmpty())
                m_selectionData->setCustomData(customData.createSharedBuffer());
        }
        return;
    }

    m_changeCount = platformStrategies()->pasteboardStrategy()->writeCustomData(data, m_name, context());
}

void Pasteboard::write(const Color&)
{
}

int64_t Pasteboard::changeCount() const
{
    if (m_selectionData)
        return 0;
    return platformStrategies()->pasteboardStrategy()->changeCount(m_name);
}

} // namespace WebCore
