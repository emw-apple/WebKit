/*
 * Copyright (C) 2015-2018 Apple Inc. All rights reserved.
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
#include "HTMLAttachmentElement.h"

#if ENABLE(ATTACHMENT_ELEMENT)

#include "AddEventListenerOptions.h"
#include "AttachmentAssociatedElement.h"
#include "AttachmentElementClient.h"
#include "CSSPropertyNames.h"
#include "CSSUnits.h"
#include "DOMRectReadOnly.h"
#include "DOMURL.h"
#include "Document.h"
#include "Editor.h"
#include "ElementInlines.h"
#include "EventNames.h"
#include "File.h"
#include "HTMLButtonElement.h"
#include "HTMLDivElement.h"
#include "HTMLElementTypeHelpers.h"
#include "HTMLImageElement.h"
#include "HTMLNames.h"
#include "HTMLStyleElement.h"
#include "LocalFrame.h"
#include "Logging.h"
#include "MIMETypeRegistry.h"
#include "MouseEvent.h"
#include "NodeName.h"
#include "RenderAttachment.h"
#include "RenderObjectInlines.h"
#include "ShadowRoot.h"
#include "SharedBuffer.h"
#include "UserAgentStyleSheets.h"
#include <pal/FileSizeFormatter.h>
#include <unicode/ubidi.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/UUID.h>
#include <wtf/URLParser.h>
#include <wtf/text/MakeString.h>
#include <wtf/unicode/CharacterNames.h>

#if ENABLE(SERVICE_CONTROLS)
#include "ImageControlsMac.h"
#endif

#if PLATFORM(COCOA)
#include "UTIUtilities.h"
#endif

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(HTMLAttachmentElement);

using namespace HTMLNames;

#if PLATFORM(VISION)
constexpr float attachmentIconSize = 40;
#elif PLATFORM(IOS_FAMILY)
constexpr float attachmentIconSize = 72;
#else
constexpr float attachmentIconSize = 52;
#endif

// FIXME: Remove after rdar://99228361 is fixed.
#define ATTACHMENT_LOG_DOCUMENT_TRAFFIC !RELEASE_LOG_DISABLED
#if ATTACHMENT_LOG_DOCUMENT_TRAFFIC
// Given a StackTrace, output one minimally-sized function identifier per line, so that more frames can fit in a log message.
static CString compactStackTrace(StackTrace& stackTrace)
{
    StringPrintStream stack;
    stackTrace.forEachFrame([&stack](int, void*, const char* fullName) {
        constexpr size_t maxWorkLength = 1023;
        auto name = StringView::fromLatin1(fullName ? fullName : "?").left(maxWorkLength);
        for (const auto& prefix : { "auto void "_s, "auto "_s }) {
            if (name.startsWith(prefix)) {
                name = name.substring(prefix.length());
                break;
            }
        }

        if (name.startsWith("decltype("_s)) {
            int depth = 1;
            for (unsigned i = "decltype("_s.length(); i < name.length(); ++i) {
                auto c = name[i];
                if (c == ')') {
                    if (!--depth) {
                        name = name.substring(i + 1);
                        if (name.startsWith(" "_s))
                            name = name.substring(" "_s.length());
                        break;
                    }
                } else if (c == '(')
                    ++depth;
            }
        }

        if (name.startsWith("std::"_s))
            return;

        for (const auto& prefix : { "WebCore::"_s, "WebKit::"_s, "IPC::"_s }) {
            if (name.startsWith(prefix)) {
                name = name.substring(prefix.length());
                break;
            }
        }

        for (unsigned i = 0; i < name.length(); ++i) {
            auto c = name[i];
            // If we find '(' first, assume it's the function parameter list, drop it and whatever follows.
            if (c == '(') {
                name = name.left(i);
                break;
            }
            // If we find '[' first, assume it's an Objective C method call, keep everything.
            if (c == '[')
                break;
        }

        constexpr unsigned maxLen = 48;
        name = name.left(maxLen);

        stack.print("\n> "_s, name);
    });
    return stack.toCString();
}
#endif // ATTACHMENT_LOG_DOCUMENT_TRAFFIC

HTMLAttachmentElement::HTMLAttachmentElement(const QualifiedName& tagName, Document& document)
    : HTMLElement(tagName, document)
{
    ASSERT(hasTagName(attachmentTag));
}

HTMLAttachmentElement::~HTMLAttachmentElement() = default;

Ref<HTMLAttachmentElement> HTMLAttachmentElement::create(const QualifiedName& tagName, Document& document)
{
    Ref attachment = adoptRef(*new HTMLAttachmentElement(tagName, document));
    if (document.settings().attachmentWideLayoutEnabled()) {
        ASSERT(attachment->m_implementation == Implementation::NarrowLayout);
        ASSERT(!attachment->renderer()); // Switch to wide-layout style *must* be done before renderer is created!
        attachment->m_implementation = Implementation::WideLayout;
        attachment->ensureUserAgentShadowRoot();
    }
    return attachment;
}

void HTMLAttachmentElement::didAddUserAgentShadowRoot(ShadowRoot& root)
{
    if (m_implementation == Implementation::WideLayout)
        ensureWideLayoutShadowTree(root);
}

static const AtomString& attachmentContainerIdentifier()
{
    static MainThreadNeverDestroyed<const AtomString> identifier("attachment-container"_s);
    return identifier;
}

static const AtomString& attachmentBackgroundIdentifier()
{
    static MainThreadNeverDestroyed<const AtomString> identifier("attachment-background"_s);
    return identifier;
}

static const AtomString& attachmentPreviewAreaIdentifier()
{
    static MainThreadNeverDestroyed<const AtomString> identifier("attachment-preview-area"_s);
    return identifier;
}

static const AtomString& attachmentPlaceholderIdentifier()
{
    static MainThreadNeverDestroyed<const AtomString> identifier("attachment-placeholder"_s);
    return identifier;
}

static const AtomString& attachmentIconIdentifier()
{
    static MainThreadNeverDestroyed<const AtomString> identifier("attachment-icon"_s);
    return identifier;
}

static const AtomString& attachmentProgressIdentifier()
{
    static MainThreadNeverDestroyed<const AtomString> identifier("attachment-progress"_s);
    return identifier;
}

static const AtomString& attachmentProgressCSSProperty()
{
    static MainThreadNeverDestroyed<const AtomString> property("--progress"_s);
    return property;
}

static const AtomString& attachmentInformationAreaIdentifier()
{
    static MainThreadNeverDestroyed<const AtomString> identifier("attachment-information-area"_s);
    return identifier;
}

static const AtomString& attachmentInformationBlockIdentifier()
{
    static MainThreadNeverDestroyed<const AtomString> identifier("attachment-information-block"_s);
    return identifier;
}

static const AtomString& attachmentActionIdentifier()
{
    static MainThreadNeverDestroyed<const AtomString> identifier("attachment-action"_s);
    return identifier;
}

static const AtomString& attachmentTitleIdentifier()
{
    static MainThreadNeverDestroyed<const AtomString> identifier("attachment-title"_s);
    return identifier;
}

static const AtomString& attachmentSubtitleIdentifier()
{
    static MainThreadNeverDestroyed<const AtomString> identifier("attachment-subtitle"_s);
    return identifier;
}

static const AtomString& attachmentSaveAreaIdentifier()
{
    static MainThreadNeverDestroyed<const AtomString> identifier("attachment-save-area"_s);
    return identifier;
}

static const AtomString& attachmentSaveButtonIdentifier()
{
    static MainThreadNeverDestroyed<const AtomString> identifier("attachment-save-button"_s);
    return identifier;
}

static const AtomString& attachmentIconSizeProperty()
{
    static MainThreadNeverDestroyed<const AtomString> identifier("--icon-size"_s);
    return identifier;
}

static const AtomString& saveAtom()
{
    static MainThreadNeverDestroyed<const AtomString> identifier("save"_s);
    return identifier;
}

class AttachmentImageEventsListener final : public EventListener {
public:
    static void addToImageForAttachment(HTMLImageElement& image, HTMLAttachmentElement& attachment)
    {
        auto listener = create(attachment);
        image.addEventListener(eventNames().loadEvent, listener, { });
        image.addEventListener(eventNames().errorEvent, listener, { });
    }

    void handleEvent(ScriptExecutionContext&, Event& event) final
    {
        const auto& type = event.type();
        if (type == eventNames().loadEvent || type == eventNames().errorEvent)
            m_attachment->dispatchEvent(Event::create(type, Event::CanBubble::No, Event::IsCancelable::No));
        else
            ASSERT_NOT_REACHED();
    }

private:
    explicit AttachmentImageEventsListener(HTMLAttachmentElement& attachment)
        : EventListener(CPPEventListenerType)
        , m_attachment(attachment)
    {
    }

    static Ref<AttachmentImageEventsListener> create(HTMLAttachmentElement& attachment) { return adoptRef(*new AttachmentImageEventsListener(attachment)); }

    WeakPtr<HTMLAttachmentElement, WeakPtrImplWithEventTargetData> m_attachment;
};

template <typename ElementType>
static Ref<ElementType> createContainedElement(HTMLElement& container, const AtomString& id, String&& textContent = { })
{
    Ref<ElementType> element = ElementType::create(container.document());
    element->setIdAttribute(id);
    if (!textContent.isEmpty())
        element->setTextContent(WTFMove(textContent));
    container.appendChild(element);
    return element;
}

void HTMLAttachmentElement::ensureWideLayoutShadowTree(ShadowRoot& root)
{
    ASSERT(m_implementation == Implementation::WideLayout);
    if (m_titleElement)
        return;

    static MainThreadNeverDestroyed<const String> shadowStyle(StringImpl::createWithoutCopying(attachmentElementShadowUserAgentStyleSheet));
    auto style = HTMLStyleElement::create(HTMLNames::styleTag, document(), false);
    style->setTextContent(String { shadowStyle });
    root.appendChild(WTFMove(style));

    m_containerElement = HTMLDivElement::create(document());
    m_containerElement->setIdAttribute(attachmentContainerIdentifier());
    m_containerElement->setInlineStyleCustomProperty(attachmentIconSizeProperty(), makeString(attachmentIconSize, "px"_s));
    root.appendChild(*m_containerElement);

    auto background = createContainedElement<HTMLDivElement>(*m_containerElement, attachmentBackgroundIdentifier());

    auto previewArea = createContainedElement<HTMLDivElement>(background, attachmentPreviewAreaIdentifier());

    m_imageElement = createContainedElement<HTMLImageElement>(previewArea, attachmentIconIdentifier());
    AttachmentImageEventsListener::addToImageForAttachment(*m_imageElement, *this);
    updateImage();

    m_placeholderElement = createContainedElement<HTMLDivElement>(previewArea, attachmentPlaceholderIdentifier());

    m_progressElement = createContainedElement<HTMLDivElement>(previewArea, attachmentProgressIdentifier());
    updateProgress(attributeWithoutSynchronization(progressAttr));

    auto informationArea = createContainedElement<HTMLDivElement>(background, attachmentInformationAreaIdentifier());

    m_informationBlock = createContainedElement<HTMLDivElement>(informationArea, attachmentInformationBlockIdentifier());

    m_actionTextElement = createContainedElement<HTMLDivElement>(*m_informationBlock, attachmentActionIdentifier(), String { attachmentActionForDisplay() });
    m_actionTextElement->setAttributeWithoutSynchronization(HTMLNames::dirAttr, autoAtom());

    m_titleElement = createContainedElement<HTMLDivElement>(*m_informationBlock, attachmentTitleIdentifier(), String { attachmentTitleForDisplay() });
    m_titleElement->setAttributeWithoutSynchronization(HTMLNames::dirAttr, autoAtom());

    m_subtitleElement = createContainedElement<HTMLDivElement>(*m_informationBlock, attachmentSubtitleIdentifier(), String { attachmentSubtitleForDisplay() });
    m_subtitleElement->setAttributeWithoutSynchronization(HTMLNames::dirAttr, autoAtom());

    updateSaveButton(!attributeWithoutSynchronization(saveAttr).isNull());
}

class AttachmentSaveEventListener final : public EventListener {
public:
    static Ref<AttachmentSaveEventListener> create(HTMLAttachmentElement& attachment) { return adoptRef(*new AttachmentSaveEventListener(attachment)); }

    void handleEvent(ScriptExecutionContext&, Event& event) final
    {
        if (isAnyClick(event)) {
            auto& mouseEvent = downcast<MouseEvent>(event);
            auto copiedEvent = MouseEvent::create(saveAtom(), Event::CanBubble::No, Event::IsCancelable::No, Event::IsComposed::No, MonotonicTime::now(),
                mouseEvent.view(), mouseEvent.detail(), mouseEvent.screenX(), mouseEvent.screenY(), mouseEvent.clientX(), mouseEvent.clientY(),
                mouseEvent.modifierKeys(), mouseEvent.button(), mouseEvent.buttons(), mouseEvent.syntheticClickType(), nullptr);

            event.preventDefault();
            event.stopPropagation();
            event.stopImmediatePropagation();

            m_attachment->dispatchEvent(copiedEvent);
        } else
            ASSERT_NOT_REACHED();
    }

private:
    explicit AttachmentSaveEventListener(HTMLAttachmentElement& attachment)
        : EventListener(CPPEventListenerType)
        , m_attachment(attachment)
    {
    }

    WeakPtr<HTMLAttachmentElement, WeakPtrImplWithEventTargetData> m_attachment;
};

void HTMLAttachmentElement::updateProgress(const AtomString& progress)
{
    if (!m_progressElement)
        return;

    bool validProgress = false;
    float value = progress.toFloat(&validProgress);
    if (validProgress && std::isfinite(value)) {
        m_imageElement->setInlineStyleProperty(CSSPropertyDisplay, CSSValueNone);
        if (!value) {
            m_placeholderElement->removeInlineStyleProperty(CSSPropertyDisplay);
            m_progressElement->setInlineStyleProperty(CSSPropertyDisplay, CSSValueNone);
            m_progressElement->removeInlineStyleCustomProperty(attachmentProgressCSSProperty());
            return;
        }
        m_placeholderElement->setInlineStyleProperty(CSSPropertyDisplay, CSSValueNone);
        m_progressElement->removeInlineStyleProperty(CSSPropertyDisplay);
        m_progressElement->setInlineStyleCustomProperty(attachmentProgressCSSProperty(), (value < 0.0) ? "0"_s : (value > 1.0) ? "1"_s : progress);
        return;
    }

    m_imageElement->removeInlineStyleProperty(CSSPropertyDisplay);
    m_placeholderElement->setInlineStyleProperty(CSSPropertyDisplay, CSSValueNone);
    m_progressElement->setInlineStyleProperty(CSSPropertyDisplay, CSSValueNone);
    m_progressElement->removeInlineStyleCustomProperty(attachmentProgressCSSProperty());
}

void HTMLAttachmentElement::updateSaveButton(bool show)
{
    if (!show) {
        if (m_saveButton) {
            m_informationBlock->removeChild(*m_saveArea);
            m_saveButton = nullptr;
            m_saveArea = nullptr;
        }
        return;
    }

    if (!m_saveButton && m_titleElement) {
        m_saveArea = createContainedElement<HTMLDivElement>(*m_informationBlock, attachmentSaveAreaIdentifier());

        m_saveButton = createContainedElement<HTMLButtonElement>(*m_saveArea, attachmentSaveButtonIdentifier());
        m_saveButton->addEventListener(eventNames().clickEvent, AttachmentSaveEventListener::create(*this), { });
        m_saveButton->addEventListener(eventNames().auxclickEvent, AttachmentSaveEventListener::create(*this), { });
    }
}

DOMRectReadOnly* HTMLAttachmentElement::saveButtonClientRect() const
{
    if (!m_saveButton)
        return nullptr;

    bool unusedIsReplaced;
    auto rect = m_saveButton->pixelSnappedAbsoluteBoundingRect(&unusedIsReplaced);
    m_saveButtonClientRect = DOMRectReadOnly::create(rect.x(), rect.y(), rect.width(), rect.height());
    return m_saveButtonClientRect.get();
}

HTMLElement* HTMLAttachmentElement::wideLayoutImageElement() const
{
    return m_imageElement.get();
}

RenderPtr<RenderElement> HTMLAttachmentElement::createElementRenderer(RenderStyle&& style, const RenderTreePosition&)
{
    return createRenderer<RenderAttachment>(*this, WTFMove(style));
}

void HTMLAttachmentElement::invalidateRendering()
{
    if (auto* renderer = this->renderer()) {
        renderer->setNeedsLayout();
        renderer->repaint();
    }
}

String HTMLAttachmentElement::getAttachmentIdentifier(HTMLElement& element)
{
    RefPtr attachmentAssociatedElement = element.asAttachmentAssociatedElement();
    if (!attachmentAssociatedElement)
        return nullString();

    if (RefPtr attachment = attachmentAssociatedElement->attachmentElement())
        return attachment->uniqueIdentifier();

    Ref document = element.document();
    auto attachment = create(HTMLNames::attachmentTag, document);
    auto identifier = attachment->ensureUniqueIdentifier();

    document->registerAttachmentIdentifier(identifier, *attachmentAssociatedElement);
    attachmentAssociatedElement->setAttachmentElement(WTFMove(attachment));

    return identifier;
}

void HTMLAttachmentElement::copyNonAttributePropertiesFromElement(const Element& source)
{
    m_uniqueIdentifier = downcast<HTMLAttachmentElement>(source).uniqueIdentifier();
    HTMLElement::copyNonAttributePropertiesFromElement(source);
}

URL HTMLAttachmentElement::archiveResourceURL(const String& identifier)
{
    auto resourceURL = URL({ }, "applewebdata://attachment/"_s);
    resourceURL.setPath(identifier);
    return resourceURL;
}

File* HTMLAttachmentElement::file() const
{
    return m_file.get();
}

URL HTMLAttachmentElement::blobURL() const
{
    return { { }, attributeWithoutSynchronization(HTMLNames::webkitattachmentbloburlAttr).string() };
}

void HTMLAttachmentElement::setFile(RefPtr<File>&& file, UpdateDisplayAttributes updateAttributes)
{
    m_file = WTFMove(file);

    if (updateAttributes == UpdateDisplayAttributes::Yes) {
        if (m_file) {
            setAttributeWithoutSynchronization(HTMLNames::titleAttr, AtomString { m_file->name() });
            setAttributeWithoutSynchronization(subtitleAttr, PAL::fileSizeDescription(m_file->size()));
            setAttributeWithoutSynchronization(HTMLNames::typeAttr, AtomString { m_file->type() });
        } else {
            removeAttribute(HTMLNames::titleAttr);
            removeAttribute(HTMLNames::subtitleAttr);
            removeAttribute(HTMLNames::typeAttr);
        }
    }

    setNeedsIconRequest();
    invalidateRendering();
}

#if ATTACHMENT_LOG_DOCUMENT_TRAFFIC
class AttachmentEvent {
public:
    uintptr_t attachment() const { return m_attachment; }
    uintptr_t document() const { return m_document; }
    String uniqueIdentifier() const { return m_uniqueIdentifier; }
    WTF::MonotonicTime time() const { return m_time; }
    StackTrace& stackTrace() const { return *m_stackTrace; }

    void capture(const HTMLAttachmentElement& a, WTF::MonotonicTime t)
    {
        m_attachment = reinterpret_cast<uintptr_t>(&a);
        m_document = reinterpret_cast<uintptr_t>(&a.document());
        m_uniqueIdentifier = a.uniqueIdentifier();
        ASSERT(!!t);
        m_time = t;
        m_stackTrace = StackTrace::captureStackTrace(64);
    }

    void reset()
    {
        m_attachment = 0;
        m_stackTrace = 0;
    }

    explicit operator bool() const
    {
        ASSERT(!m_attachment == !m_stackTrace);
        return !!m_attachment;
    }

private:
    uintptr_t m_attachment { };
    uintptr_t m_document { };
    String m_uniqueIdentifier;
    WTF::MonotonicTime m_time;
    std::unique_ptr<StackTrace> m_stackTrace;
};

static AttachmentEvent& lastInsertionInDocument()
{
    IGNORE_CLANG_WARNINGS_BEGIN("exit-time-destructors")
    static AttachmentEvent event;
    IGNORE_CLANG_WARNINGS_END
    return event;
}

static AttachmentEvent& lastRemovalFromDocument()
{
    IGNORE_CLANG_WARNINGS_BEGIN("exit-time-destructors")
    static AttachmentEvent event;
    IGNORE_CLANG_WARNINGS_END
    return event;
}

static bool shouldMonitorDocumentTraffic(Document& document)
{
    static constexpr auto sequenceMaxTime = 1_s .seconds();
    return document.monotonicTimestamp() < sequenceMaxTime;
}
#endif // ATTACHMENT_LOG_DOCUMENT_TRAFFIC

Node::InsertedIntoAncestorResult HTMLAttachmentElement::insertedIntoAncestor(InsertionType type, ContainerNode& ancestor)
{
    auto result = HTMLElement::insertedIntoAncestor(type, ancestor);
    if (isWideLayout()) {
        setInlineStyleProperty(CSSPropertyMarginLeft, 1, CSSUnitType::CSS_PX);
        setInlineStyleProperty(CSSPropertyMarginRight, 1, CSSUnitType::CSS_PX);
        setInlineStyleProperty(CSSPropertyMarginTop, 1, CSSUnitType::CSS_PX);
        setInlineStyleProperty(CSSPropertyMarginBottom, 1, CSSUnitType::CSS_PX);
    }

#if ATTACHMENT_LOG_DOCUMENT_TRAFFIC
    if (type.connectedToDocument && shouldMonitorDocumentTraffic(document())) {
        auto& lastInsertion = lastInsertionInDocument();
        auto& lastRemoval = lastRemovalFromDocument();
        auto now = WTF::MonotonicTime::now();
        if (lastInsertion && lastRemoval && lastRemoval.attachment() != reinterpret_cast<uintptr_t>(this) && lastRemoval.document() == reinterpret_cast<uintptr_t>(&document())) {
            RELEASE_LOG(Editing, "HTMLAttachmentElement - quick insert(A)-remove(A)-insert(B) within %fs of the first document[%p] load, stacks below:", document().monotonicTimestamp(), reinterpret_cast<const void*>(lastRemoval.document()));
            RELEASE_LOG(Editing, "HTMLAttachmentElement[%p uuid=%s] - 1st insertion %fms ago:%s", reinterpret_cast<const void*>(lastInsertion.attachment()), lastInsertion.uniqueIdentifier().utf8().data(), (now - lastInsertion.time()).milliseconds(), compactStackTrace(lastInsertion.stackTrace()).data());
            lastInsertion.reset();
            RELEASE_LOG(Editing, "HTMLAttachmentElement[%p uuid=%s] - removal %fms ago:%s", reinterpret_cast<const void*>(lastRemoval.attachment()), lastRemoval.uniqueIdentifier().utf8().data(), (now - lastRemoval.time()).milliseconds(), compactStackTrace(lastRemoval.stackTrace()).data());
            lastRemoval.reset();
            lastInsertion.capture(*this, now);
            RELEASE_LOG(Editing, "HTMLAttachmentElement[%p uuid=%s] - 2nd insertion:%s", reinterpret_cast<const void*>(lastInsertion.attachment()), lastInsertion.uniqueIdentifier().utf8().data(), compactStackTrace(lastInsertion.stackTrace()).data());
        } else {
            lastInsertion.capture(*this, now);
            lastRemoval.reset();
        }
    }
#endif // ATTACHMENT_LOG_DOCUMENT_TRAFFIC

    if (type.connectedToDocument)
        document().didInsertAttachmentElement(*this);
    return result;
}

void HTMLAttachmentElement::removedFromAncestor(RemovalType type, ContainerNode& ancestor)
{
    HTMLElement::removedFromAncestor(type, ancestor);

#if ATTACHMENT_LOG_DOCUMENT_TRAFFIC
    if (type.disconnectedFromDocument && shouldMonitorDocumentTraffic(document())) {
        if (auto& lastInsertion = lastInsertionInDocument(); lastInsertion && lastInsertion.attachment() == reinterpret_cast<uintptr_t>(this))
            lastRemovalFromDocument().capture(*this, WTF::MonotonicTime::now());
    }
#endif // ATTACHMENT_LOG_DOCUMENT_TRAFFIC

    if (type.disconnectedFromDocument)
        document().didRemoveAttachmentElement(*this);
}

String HTMLAttachmentElement::ensureUniqueIdentifier()
{
    if (m_uniqueIdentifier.isEmpty())
        m_uniqueIdentifier = createVersion4UUIDString();
    return m_uniqueIdentifier;
}

void HTMLAttachmentElement::setUniqueIdentifier(const String& uniqueIdentifier)
{
    if (m_uniqueIdentifier == uniqueIdentifier)
        return;

    m_uniqueIdentifier = uniqueIdentifier;

    if (auto associatedElement = this->associatedElement())
        associatedElement->didUpdateAttachmentIdentifier();
}

AttachmentAssociatedElement* HTMLAttachmentElement::associatedElement() const
{
    if (RefPtr host = shadowHost())
        return host->asAttachmentAssociatedElement();
    return nullptr;
}

AttachmentAssociatedElementType HTMLAttachmentElement::associatedElementType() const
{
    if (RefPtr associatedElement = this->associatedElement())
        return associatedElement->attachmentAssociatedElementType();

    return AttachmentAssociatedElementType::None;
}

void HTMLAttachmentElement::attributeChanged(const QualifiedName& name, const AtomString& oldValue, const AtomString& newValue, AttributeModificationReason attributeModificationReason)
{
    switch (name.nodeName()) {
    case AttributeNames::actionAttr:
    case AttributeNames::subtitleAttr:
    case AttributeNames::titleAttr:
    case AttributeNames::typeAttr:
        invalidateRendering();
        break;
    case AttributeNames::progressAttr:
        if (m_implementation == Implementation::NarrowLayout)
            invalidateRendering();
        break;
    default:
        break;
    }

    HTMLElement::attributeChanged(name, oldValue, newValue, attributeModificationReason);

    switch (name.nodeName()) {
    case AttributeNames::actionAttr:
        if (m_actionTextElement)
            m_actionTextElement->setTextContent(String(attachmentActionForDisplay()));
        break;
    case AttributeNames::titleAttr:
        if (m_titleElement)
            m_titleElement->setTextContent(attachmentTitleForDisplay());
        setNeedsIconRequest();
        break;
    case AttributeNames::subtitleAttr:
        if (m_subtitleElement)
            m_subtitleElement->setTextContent(String(attachmentSubtitleForDisplay()));
        break;
    case AttributeNames::progressAttr:
        updateProgress(newValue);
        break;
    case AttributeNames::saveAttr:
        updateSaveButton(!newValue.isNull());
        break;
    case AttributeNames::typeAttr:
#if ENABLE(SERVICE_CONTROLS)
        if (attachmentType() == "application/pdf"_s) {
            setImageMenuEnabled(true);
            ImageControlsMac::updateImageControls(*this);
        }
#endif
        setNeedsIconRequest();
        break;
    default:
        break;
    }
}

String HTMLAttachmentElement::attachmentTitle() const
{
    auto& title = attributeWithoutSynchronization(titleAttr);
    if (!title.isEmpty())
        return title;
    return m_file ? m_file->name() : String();
}

const AtomString& HTMLAttachmentElement::attachmentSubtitle() const
{
    return attributeWithoutSynchronization(subtitleAttr);
}

const AtomString& HTMLAttachmentElement::attachmentActionForDisplay() const
{
    return attributeWithoutSynchronization(actionAttr);
}

String HTMLAttachmentElement::attachmentTitleForDisplay() const
{
    auto title = attachmentTitle();
    auto indexOfLastDot = title.reverseFind('.');
    if (indexOfLastDot == notFound)
        return title;

    auto filename = StringView(title).left(indexOfLastDot);
    auto extension = StringView(title).substring(indexOfLastDot);

    if (isWideLayout() && !filename.is8Bit() && ubidi_getBaseDirection(filename.span16().data(), filename.length()) == UBIDI_RTL) {
        // The filename is deemed RTL, it should be exposed as RTL overall, but keeping the extension to the right.
        return makeString(
            rightToLeftMark, // Make this whole text appear as RTL, the element's `dir="auto"` will right-align and put ellipsis on the left (if needed)
            leftToRightIsolate, // Isolate the filename+extension, and force LTR to ensure that the extension always stays on the right.
            firstStrongIsolate, // Isolate the filename.
            filename, // Note: The filename contains its own bidi characters.
            popDirectionalIsolate, // End isolation of the filename.
            zeroWidthSpace, // Add a preferred breakpoint before the extension when word-wrapping (so the extension doesn't get split).
            extension,
            popDirectionalIsolate // And end the filename+extension LTR isolation.
        );
    }

    // Non-RTL or narrow layout: Keep the extension to the right, but the overall direction doesn't need to be exposed.
    return makeString(
        leftToRightMark, // Force LTR to ensure that the extension always stays on the right.
        firstStrongIsolate, // Isolate the filename.
        filename, // Note: The filename contains its own bidi characters.
        popDirectionalIsolate, // End isolation of the filename.
        zeroWidthSpace, // Add a preferred breakpoint before the extension when word-wrapping (so the extension doesn't get split).
        extension
    );
}

const AtomString& HTMLAttachmentElement::attachmentSubtitleForDisplay() const
{
    return attachmentSubtitle();
}

String HTMLAttachmentElement::attachmentType() const
{
    return attributeWithoutSynchronization(typeAttr);
}

String HTMLAttachmentElement::attachmentPath() const
{
    return attributeWithoutSynchronization(webkitattachmentpathAttr);
}

void HTMLAttachmentElement::updateAttributes(std::optional<uint64_t>&& newFileSize, const AtomString& newContentType, const AtomString& newFilename)
{
    RefPtr<HTMLImageElement> enclosingImage;
    if (auto associatedElement = this->associatedElement())
        enclosingImage = dynamicDowncast<HTMLImageElement>(associatedElement->asHTMLElement());

    if (!newFilename.isNull()) {
        if (enclosingImage)
            enclosingImage->setAttributeWithoutSynchronization(HTMLNames::altAttr, newFilename);
        setAttributeWithoutSynchronization(HTMLNames::titleAttr, newFilename);
    } else {
        if (enclosingImage)
            enclosingImage->removeAttribute(HTMLNames::altAttr);
        removeAttribute(HTMLNames::titleAttr);
    }

    if (!newContentType.isNull())
        setAttributeWithoutSynchronization(HTMLNames::typeAttr, newContentType);
    else
        removeAttribute(HTMLNames::typeAttr);

    if (newFileSize)
        setAttributeWithoutSynchronization(subtitleAttr, PAL::fileSizeDescription(*newFileSize));
    else
        removeAttribute(subtitleAttr);

    setNeedsIconRequest();
    invalidateRendering();
}

static bool mimeTypeIsSuitableForInlineImageAttachment(const String& mimeType)
{
    return MIMETypeRegistry::isSupportedImageMIMEType(mimeType) || MIMETypeRegistry::isPDFMIMEType(mimeType);
}

void HTMLAttachmentElement::updateAssociatedElementWithData(const String& contentType, Ref<FragmentedSharedBuffer>&& buffer)
{
    if (buffer->isEmpty())
        return;

    RefPtr associatedElement = this->associatedElement();
    if (!associatedElement)
        return;

    String mimeType = contentType;
#if PLATFORM(COCOA)
    if (isDeclaredUTI(contentType))
        mimeType = MIMETypeFromUTI(contentType);
#endif

    if (!mimeTypeIsSuitableForInlineImageAttachment(mimeType))
        return;

    auto associatedElementType = associatedElement->attachmentAssociatedElementType();
    associatedElement->asHTMLElement().setAttributeWithoutSynchronization((associatedElementType == AttachmentAssociatedElementType::Source) ? HTMLNames::srcsetAttr : HTMLNames::srcAttr, AtomString { DOMURL::createObjectURL(document(), Blob::create(protectedDocument().ptr(), buffer->extractData(), mimeType)) });
}

void HTMLAttachmentElement::updateImage()
{
    if (!m_imageElement)
        return;

    if (!m_iconForWideLayout.isEmpty()) {
        dispatchEvent(Event::create(eventNames().loadeddataEvent, Event::CanBubble::No, Event::IsCancelable::No));
        m_imageElement->setAttributeWithoutSynchronization(srcAttr, AtomString { DOMURL::createObjectURL(document(), Blob::create(protectedDocument().ptr(), Vector<uint8_t>(m_iconForWideLayout), "image/png"_s)) });
        return;
    }

    m_imageElement->removeAttribute(srcAttr);
}

void HTMLAttachmentElement::updateIconForNarrowLayout(const RefPtr<Image>& icon, const WebCore::FloatSize& iconSize)
{
    ASSERT(!isWideLayout());
    if (!icon) {
        dispatchEvent(Event::create(eventNames().loadingerrorEvent, Event::CanBubble::No, Event::IsCancelable::No));
        return;
    }
    m_icon = icon;
    m_iconSize = iconSize;
    invalidateRendering();
    dispatchEvent(Event::create(eventNames().loadEvent, Event::CanBubble::No, Event::IsCancelable::No));
}

void HTMLAttachmentElement::updateIconForWideLayout(Vector<uint8_t>&& iconSrcData)
{
    ASSERT(isWideLayout());
    if (iconSrcData.isEmpty()) {
        dispatchEvent(Event::create(eventNames().loadingerrorEvent, Event::CanBubble::No, Event::IsCancelable::No));
        return;
    }
    m_iconForWideLayout = WTFMove(iconSrcData);
    updateImage();
}

void HTMLAttachmentElement::setNeedsIconRequest()
{
    m_needsIconRequest = true;
}

void HTMLAttachmentElement::requestWideLayoutIconIfNeeded()
{
    if (!m_needsIconRequest)
        return;

    if (!document().page() || !document().page()->attachmentElementClient())
        return;

    m_needsIconRequest = false;

    if (!m_imageElement)
        return;

// FIXME: Remove after rdar://136373445 is fixed.
#if PLATFORM(MAC)
    RELEASE_LOG(Editing, "HTMLAttachmentElement[uuid=%s] requestAttachmentIcon with type='%s'", uniqueIdentifier().utf8().data(), attachmentType().utf8().data());
#endif

    dispatchEvent(Event::create(eventNames().beforeloadEvent, Event::CanBubble::No, Event::IsCancelable::No));
    document().page()->attachmentElementClient()->requestAttachmentIcon(uniqueIdentifier(), FloatSize(attachmentIconSize, attachmentIconSize));
}

void HTMLAttachmentElement::requestIconIfNeededWithSize(const FloatSize& size)
{
    ASSERT(!isWideLayout());
    if (!m_needsIconRequest)
        return;

    if (!document().page() || !document().page()->attachmentElementClient())
        return;

    m_needsIconRequest = false;

    queueTaskToDispatchEvent(TaskSource::InternalAsyncTask, Event::create(eventNames().beforeloadEvent, Event::CanBubble::No, Event::IsCancelable::No));
    document().page()->attachmentElementClient()->requestAttachmentIcon(uniqueIdentifier(), size);
}

#if ENABLE(SERVICE_CONTROLS)
bool HTMLAttachmentElement::childShouldCreateRenderer(const Node& child) const
{
    return hasShadowRootParent(child) && HTMLElement::childShouldCreateRenderer(child);
}
#endif

} // namespace WebCore

#endif // ENABLE(ATTACHMENT_ELEMENT)
