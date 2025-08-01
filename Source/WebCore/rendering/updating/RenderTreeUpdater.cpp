/*
 * Copyright (C) 2016-2021 Apple Inc. All rights reserved.
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
#include "RenderTreeUpdater.h"

#include "AXObjectCache.h"
#include "ComposedTreeAncestorIterator.h"
#include "ComposedTreeIterator.h"
#include "ContainerNodeInlines.h"
#include "Document.h"
#include "Element.h"
#include "FrameSelection.h"
#include "HTMLSlotElement.h"
#include "LayoutState.h"
#include "LayoutTreeBuilder.h"
#include "LegacyRenderSVGResource.h"
#include "LocalFrameView.h"
#include "LocalFrameViewLayoutContext.h"
#include "NodeInlines.h"
#include "NodeRenderStyle.h"
#include "PseudoElement.h"
#include "RenderBoxInlines.h"
#include "RenderDescendantIterator.h"
#include "RenderElementInlines.h"
#include "RenderFlexibleBox.h"
#include "RenderInline.h"
#include "RenderLayer.h"
#include "RenderListItem.h"
#include "RenderMultiColumnFlow.h"
#include "RenderMultiColumnSet.h"
#include "RenderObjectInlines.h"
#include "RenderStyleConstants.h"
#include "RenderStyleInlines.h"
#include "RenderTreeUpdaterGeneratedContent.h"
#include "RenderTreeUpdaterViewTransition.h"
#include "RenderView.h"
#include "SVGElement.h"
#include "StyleResolver.h"
#include "StyleTreeResolver.h"
#include "TextManipulationController.h"
#include <wtf/Scope.h>
#include <wtf/SystemTracing.h>

#if ENABLE(CONTENT_CHANGE_OBSERVER)
#include "ContentChangeObserver.h"
#endif

namespace WebCore {

RenderTreeUpdater::Parent::Parent(ContainerNode& root)
    : element(dynamicDowncast<Element>(root))
    , renderTreePosition(RenderTreePosition(*root.renderer()))
{
}

RenderTreeUpdater::Parent::Parent(Element& element, const Style::ElementUpdate* update)
    : element(&element)
    , update(update)
    , renderTreePosition(element.renderer() ? std::make_optional(RenderTreePosition(*element.renderer())) : std::nullopt)
{
}

RenderTreeUpdater::RenderTreeUpdater(Document& document, Style::PostResolutionCallbackDisabler&)
    : m_document(document)
    , m_generatedContent(makeUniqueRef<GeneratedContent>(*this))
    , m_viewTransition(makeUniqueRef<ViewTransition>(*this))
    , m_builder(renderView())
{
}

RenderTreeUpdater::~RenderTreeUpdater() = default;

static Element* findRenderingAncestor(Node& node)
{
    for (auto& ancestor : composedTreeAncestors(node)) {
        if (ancestor.renderer())
            return &ancestor;
        if (!ancestor.hasDisplayContents())
            return nullptr;
    }
    return nullptr;
}

static ContainerNode* findRenderingRoot(ContainerNode& node)
{
    if (node.renderer())
        return &node;
    return findRenderingAncestor(node);
}

void RenderTreeUpdater::commit(std::unique_ptr<Style::Update> styleUpdate)
{
    ASSERT(m_document.ptr() == &styleUpdate->document());

    if (!m_document->shouldCreateRenderers() || !m_document->renderView())
        return;

    TraceScope scope(RenderTreeBuildStart, RenderTreeBuildEnd);

    m_styleUpdate = WTFMove(styleUpdate);

    updateRebuildRoots();

    updateRenderViewStyle();

    for (auto& root : m_styleUpdate->roots()) {
        if (&root->document() != m_document.ptr())
            continue;
        auto* renderingRoot = findRenderingRoot(*root);
        if (!renderingRoot)
            continue;
        updateRenderTree(*renderingRoot);
    }

    generatedContent().updateRemainingQuotes();
    generatedContent().updateCounters();

    m_builder.updateAfterDescendants(renderView());

    m_styleUpdate = nullptr;
}

void RenderTreeUpdater::updateRebuildRoots()
{
    auto findNewRebuildRoot = [&](auto& root) -> Element* {
        auto* renderingAncestor = findRenderingAncestor(root);
        if (!renderingAncestor)
            return nullptr;
        auto isInsideContinuation = root.renderer() && root.renderer()->parent()->isContinuation();
        auto isInsideAnonymousFlexItemWithSiblings = [&] {
            if (!is<RenderFlexibleBox>(renderingAncestor->renderer()))
                return false;
            if (!root.previousSibling() || !root.previousSibling()->renderer() || !root.nextSibling() || !root.nextSibling()->renderer())
                return false;
            // Direct children of a flex box are supposed to be individual flex items.
            if (auto* parent = root.previousSibling()->renderer()->parent(); parent && parent->isAnonymousBlock())
                return true;
            return false;
        };
        if (isInsideContinuation || isInsideAnonymousFlexItemWithSiblings() || RenderTreeBuilder::isRebuildRootForChildren(*renderingAncestor->renderer()))
            return renderingAncestor;
        return nullptr;
    };

    auto addForRebuild = [&](auto& element) {
        auto* existingUpdate = m_styleUpdate->elementUpdate(element);
        if (existingUpdate) {
            if (existingUpdate->changes.contains(Style::Change::Renderer))
                return false;
            existingUpdate->changes.add(Style::Change::Renderer);
            return true;
        }

        if (!element.renderer())
            return element.hasDisplayContents();

        auto* parent = composedTreeAncestors(element).first();
        m_styleUpdate->addElement(element, parent, Style::ElementUpdate {
            makeUnique<RenderStyle>(RenderStyle::cloneIncludingPseudoElements(element.renderer()->style())),
            Style::Change::Renderer
        });
        return true;
    };

    auto addSubtreeForRebuild = [&](auto& root) {
        if (!addForRebuild(root))
            return;
        auto descendants = composedTreeDescendants(root);
        auto it = descendants.begin();
        auto end = descendants.end();
        while (it != end) {
            auto* descendant = dynamicDowncast<Element>(*it);
            if (!descendant) {
                it.traverseNext();
                continue;
            }
            if (!addForRebuild(*descendant)) {
                it.traverseNextSkippingChildren();
                continue;
            }
            it.traverseNext();
        }
    };

    while (true) {
        auto rebuildRoots = m_styleUpdate->takeRebuildRoots();
        if (rebuildRoots.isEmpty())
            break;
        for (auto& rebuildRoot : rebuildRoots) {
            if (auto* newRebuildRoot = findNewRebuildRoot(*rebuildRoot))
                addSubtreeForRebuild(*newRebuildRoot);
        }
    }
}

static bool shouldCreateRenderer(const Element& element, const RenderElement& parentRenderer)
{
    if (!parentRenderer.canHaveChildren() && !(element.isPseudoElement() && parentRenderer.canHaveGeneratedChildren()))
        return false;
    if (parentRenderer.element() && !parentRenderer.element()->childShouldCreateRenderer(element))
        return false;
    return true;
}

void RenderTreeUpdater::updateRenderTree(ContainerNode& root)
{
    ASSERT(root.renderer());
    ASSERT(m_parentStack.isEmpty());

    m_parentStack.append(Parent(root));

    auto descendants = composedTreeDescendants(root);
    auto it = descendants.begin();
    auto end = descendants.end();

    // FIXME: https://bugs.webkit.org/show_bug.cgi?id=156172
    it.dropAssertions();

    while (it != end) {
        popParentsToDepth(it.depth());

        auto& node = *it;

        if (auto* renderer = node.renderer())
            renderTreePosition().invalidateNextSibling(*renderer);
        else if (auto* element = dynamicDowncast<Element>(node); element && element->hasDisplayContents())
            renderTreePosition().invalidateNextSibling();

        if (auto* text = dynamicDowncast<Text>(node)) {
            auto* textUpdate = m_styleUpdate->textUpdate(*text);
            bool didCreateParent = parent().update && parent().update->changes.contains(Style::Change::Renderer);
            bool mayNeedUpdateWhitespaceOnlyRenderer = renderingParent().didCreateOrDestroyChildRenderer && text->containsOnlyASCIIWhitespace();
            if (didCreateParent || textUpdate || mayNeedUpdateWhitespaceOnlyRenderer)
                updateTextRenderer(*text, textUpdate, { });

            storePreviousRenderer(*text);
            it.traverseNextSkippingChildren();
            continue;
        }

        auto& element = downcast<Element>(node);

        bool needsSVGRendererUpdate = element.needsSVGRendererUpdate();
        if (needsSVGRendererUpdate)
            updateSVGRenderer(element);

        auto* elementUpdate = m_styleUpdate->elementUpdate(element);

        // We hop through display: contents elements in findRenderingRoot, so
        // there may be other updates down the tree.
        if (!elementUpdate && !element.hasDisplayContents() && !needsSVGRendererUpdate) {
            storePreviousRenderer(element);
            it.traverseNextSkippingChildren();
            continue;
        }

        if (elementUpdate)
            updateElementRenderer(element, *elementUpdate);

        storePreviousRenderer(element);

        auto mayHaveRenderedDescendants = [&]() {
            if (element.renderer())
                return !(element.isInTopLayer() && element.renderer()->isSkippedContent());
            return element.hasDisplayContents() && shouldCreateRenderer(element, renderTreePosition().parent());
        }();

        if (!mayHaveRenderedDescendants) {
            it.traverseNextSkippingChildren();
            if (&element == element.document().documentElement())
                viewTransition().updatePseudoElementTree(nullptr, StyleDifference::Equal);
            continue;
        }

        pushParent(element, elementUpdate);

        it.traverseNext();
    }

    popParentsToDepth(0);
}

auto RenderTreeUpdater::renderingParent() -> Parent&
{
    for (unsigned i = m_parentStack.size(); i--;) {
        if (m_parentStack[i].renderTreePosition)
            return m_parentStack[i];
    }
    ASSERT_NOT_REACHED();
    return m_parentStack.last();
}

RenderTreePosition& RenderTreeUpdater::renderTreePosition()
{
    return *renderingParent().renderTreePosition;
}

void RenderTreeUpdater::pushParent(Element& element, const Style::ElementUpdate* update)
{
    m_parentStack.append(Parent(element, update));

    updateBeforeDescendants(element, update);
}

void RenderTreeUpdater::popParent()
{
    auto& parent = m_parentStack.last();
    if (parent.element)
        updateAfterDescendants(*parent.element, parent.update);

    if (&parent != &renderingParent())
        renderTreePosition().invalidateNextSibling();

    m_parentStack.removeLast();
}

void RenderTreeUpdater::popParentsToDepth(unsigned depth)
{
    ASSERT(m_parentStack.size() >= depth);

    while (m_parentStack.size() > depth)
        popParent();
}

void RenderTreeUpdater::updateBeforeDescendants(Element& element, const Style::ElementUpdate* update)
{
    if (update)
        generatedContent().updateBeforeOrAfterPseudoElement(element, *update, PseudoId::Before);

    if (auto* before = element.beforePseudoElement())
        storePreviousRenderer(*before);
}

void RenderTreeUpdater::updateAfterDescendants(Element& element, const Style::ElementUpdate* update)
{
    if (update)
        generatedContent().updateBeforeOrAfterPseudoElement(element, *update, PseudoId::After);

    auto* renderer = element.renderer();
    if (!renderer) {
        if (&element == element.document().documentElement())
            viewTransition().updatePseudoElementTree(nullptr, StyleDifference::Equal);
        return;
    }

    StyleDifference minimalStyleDifference = StyleDifference::Equal;
    if (update && update->recompositeLayer)
        minimalStyleDifference = StyleDifference::RecompositeLayer;

    generatedContent().updateBackdropRenderer(*renderer, minimalStyleDifference);
    generatedContent().updateWritingSuggestionsRenderer(*renderer, minimalStyleDifference);
    if (&element == element.document().documentElement())
        viewTransition().updatePseudoElementTree(renderer, minimalStyleDifference);

    m_builder.updateAfterDescendants(*renderer);

    if (element.hasCustomStyleResolveCallbacks() && update && update->changes.contains(Style::Change::Renderer))
        element.didAttachRenderers();
}

static bool pseudoStyleCacheIsInvalid(RenderElement* renderer, RenderStyle* newStyle)
{
    const auto& currentStyle = renderer->style();

    const auto* pseudoStyleCache = currentStyle.cachedPseudoStyles();
    if (!pseudoStyleCache)
        return false;

    for (auto& [key, value] : pseudoStyleCache->styles) {
        auto newPseudoStyle = renderer->getUncachedPseudoStyle(*value->pseudoElementIdentifier(), newStyle, newStyle);
        if (!newPseudoStyle)
            return true;
        if (*newPseudoStyle != *value) {
            newStyle->addCachedPseudoStyle(WTFMove(newPseudoStyle));
            return true;
        }
    }
    return false;
}

void RenderTreeUpdater::updateRendererStyle(RenderElement& renderer, RenderStyle&& newStyle, StyleDifference minimalStyleDifference)
{
    auto oldStyle = RenderStyle::clone(renderer.style());
    renderer.setStyle(WTFMove(newStyle), minimalStyleDifference);
    m_builder.normalizeTreeAfterStyleChange(renderer, oldStyle);
}

void RenderTreeUpdater::updateSVGRenderer(Element& element)
{
    ASSERT(element.needsSVGRendererUpdate());
    element.setNeedsSVGRendererUpdate(false);

    auto* renderer = element.renderer();
    if (!renderer)
        return;

    if (element.document().settings().layerBasedSVGEngineEnabled()) {
        renderer->setNeedsLayout();
        return;
    }

    LegacyRenderSVGResource::markForLayoutAndParentResourceInvalidation(*renderer);
}

void RenderTreeUpdater::updateElementRenderer(Element& element, const Style::ElementUpdate& elementUpdate)
{
    if (!elementUpdate.style)
        return;

#if ENABLE(CONTENT_CHANGE_OBSERVER)
    ContentChangeObserver::StyleChangeScope observingScope(m_document, element);
#endif

    auto elementUpdateStyle = RenderStyle::cloneIncludingPseudoElements(*elementUpdate.style);

    bool shouldTearDownRenderers = [&]() {
        if (element.isInTopLayer() && elementUpdate.changes.contains(Style::Change::Inherited) && elementUpdate.style->isSkippedRootOrSkippedContent())
            return true;
        return elementUpdate.changes.contains(Style::Change::Renderer) && (element.renderer() || element.hasDisplayContents());
    }();

    if (shouldTearDownRenderers) {
        if (!element.renderer()) {
            // We may be tearing down a descendant renderer cached in renderTreePosition.
            renderTreePosition().invalidateNextSibling();
        }

        // display:none cancels animations.
        auto teardownType = [&]() {
            if (!elementUpdate.style->hasDisplayAffectedByAnimations() && elementUpdate.style->display() == DisplayType::None)
                return TeardownType::RendererUpdateCancelingAnimations;
            return TeardownType::RendererUpdate;
        }();

        tearDownRenderers(element, teardownType, m_builder);

        renderingParent().didCreateOrDestroyChildRenderer = true;
    }

    bool hasDisplayContents = elementUpdate.style->display() == DisplayType::Contents;
    bool hasDisplayNonePreventingRendererCreation = elementUpdate.style->display() == DisplayType::None && !element.rendererIsNeeded(elementUpdateStyle);
    bool hasDisplayContentsOrNone = hasDisplayContents || hasDisplayNonePreventingRendererCreation;
    if (hasDisplayContentsOrNone)
        element.storeDisplayContentsOrNoneStyle(makeUnique<RenderStyle>(WTFMove(elementUpdateStyle)));
    else
        element.clearDisplayContentsOrNoneStyle();

    if (!hasDisplayContentsOrNone) {
        if (!elementUpdateStyle.containIntrinsicLogicalWidth().hasAuto())
            element.clearLastRememberedLogicalWidth();
        if (!elementUpdateStyle.containIntrinsicLogicalHeight().hasAuto())
            element.clearLastRememberedLogicalHeight();
    }

    auto scopeExit = makeScopeExit([&] {
        if (!hasDisplayContentsOrNone) {
            auto* box = element.renderBox();
            if (box && box->style().hasAutoLengthContainIntrinsicSize() && !isSkippedContentRoot(*box))
                m_document->observeForContainIntrinsicSize(element);
            else
                m_document->unobserveForContainIntrinsicSize(element);
        }
    });

    bool shouldCreateNewRenderer = !element.renderer() && !hasDisplayContentsOrNone && !(element.isInTopLayer() && renderTreePosition().parent().style().isSkippedRootOrSkippedContent());
    if (shouldCreateNewRenderer) {
        if (element.hasCustomStyleResolveCallbacks())
            element.willAttachRenderers();
        createRenderer(element, WTFMove(elementUpdateStyle));

        renderingParent().didCreateOrDestroyChildRenderer = true;
        return;
    }

    if (!element.renderer())
        return;
    auto& renderer = *element.renderer();

    if (elementUpdate.recompositeLayer) {
        updateRendererStyle(renderer, WTFMove(elementUpdateStyle), StyleDifference::RecompositeLayer);
        return;
    }

    if (!elementUpdate.changes) {
        if (pseudoStyleCacheIsInvalid(&renderer, &elementUpdateStyle)) {
            updateRendererStyle(renderer, WTFMove(elementUpdateStyle), StyleDifference::Equal);
            return;
        }
        return;
    }

    updateRendererStyle(renderer, WTFMove(elementUpdateStyle), StyleDifference::Equal);
}

void RenderTreeUpdater::createRenderer(Element& element, RenderStyle&& style)
{
    auto computeInsertionPosition = [this, &element] () {
        renderTreePosition().computeNextSibling(element);
        return renderTreePosition();
    };

    if (!shouldCreateRenderer(element, renderTreePosition().parent()))
        return;

    if (!element.rendererIsNeeded(style))
        return;

    RenderTreePosition insertionPosition = computeInsertionPosition();
    auto newRenderer = element.createElementRenderer(WTFMove(style), insertionPosition);
    if (!newRenderer)
        return;

    if (!insertionPosition.parent().isChildAllowed(*newRenderer, newRenderer->style()))
        return;

    element.setRenderer(newRenderer.get());

    newRenderer->initializeStyle();

    m_builder.attach(insertionPosition.parent(), WTFMove(newRenderer), insertionPosition.nextSibling());

    auto* textManipulationController = m_document->textManipulationControllerIfExists();
    if (textManipulationController) [[unlikely]]
        textManipulationController->didAddOrCreateRendererForNode(element);

    if (auto* cache = m_document->axObjectCache())
        cache->onRendererCreated(element);
}

bool RenderTreeUpdater::textRendererIsNeeded(const Text& textNode)
{
    auto& renderingParent = this->renderingParent();
    auto& parentRenderer = renderingParent.renderTreePosition->parent();
    if (!parentRenderer.canHaveChildren())
        return false;
    if (parentRenderer.element() && !parentRenderer.element()->childShouldCreateRenderer(textNode))
        return false;
    if (textNode.isEditingText())
        return true;
    if (!textNode.length())
        return false;
    if (!textNode.containsOnlyASCIIWhitespace())
        return true;
    if (is<RenderText>(renderingParent.previousChildRenderer))
        return true;
    // This text node has nothing but white space. We may still need a renderer in some cases.
    if (parentRenderer.isRenderTable() || parentRenderer.isRenderTableRow() || parentRenderer.isRenderTableSection() || parentRenderer.isRenderTableCol() || parentRenderer.isRenderFrameSet() || parentRenderer.isRenderGrid() || (parentRenderer.isRenderFlexibleBox() && !parentRenderer.isRenderButton()))
        return false;
    if (parentRenderer.style().preserveNewline()) // pre/pre-wrap/pre-line always make renderers.
        return true;

    auto* previousRenderer = renderingParent.previousChildRenderer;
    if (previousRenderer && previousRenderer->isBR()) // <span><br/> <br/></span>
        return false;

    if (parentRenderer.isRenderInline()) {
        // <span><div/> <div/></span>
        if (previousRenderer && !previousRenderer->isInline() && !previousRenderer->isOutOfFlowPositioned())
            return false;

        return true;
    }

    if (parentRenderer.isRenderBlock() && !parentRenderer.childrenInline() && (!previousRenderer || !previousRenderer->isInline()))
        return false;

    return renderingParent.hasPrecedingInFlowChild;
}

void RenderTreeUpdater::createTextRenderer(Text& textNode, const Style::TextUpdate* textUpdate)
{
    ASSERT(!textNode.renderer());

    auto& renderTreePosition = this->renderTreePosition();
    auto textRenderer = textNode.createTextRenderer(renderTreePosition.parent().style());

    renderTreePosition.computeNextSibling(textNode);

    if (!renderTreePosition.parent().isChildAllowed(*textRenderer, renderTreePosition.parent().style()))
        return;

    textNode.setRenderer(textRenderer.get());

    if (textUpdate && textUpdate->inheritedDisplayContentsStyle && *textUpdate->inheritedDisplayContentsStyle) {
        // Wrap text renderer into anonymous inline so we can give it a style.
        // This is to support "<div style='display:contents;color:green'>text</div>" type cases
        auto newDisplayContentsAnonymousWrapper = WebCore::createRenderer<RenderInline>(RenderObject::Type::Inline, textNode.document(), RenderStyle::clone(**textUpdate->inheritedDisplayContentsStyle));
        newDisplayContentsAnonymousWrapper->initializeStyle();
        auto& displayContentsAnonymousWrapper = *newDisplayContentsAnonymousWrapper;
        m_builder.attach(renderTreePosition.parent(), WTFMove(newDisplayContentsAnonymousWrapper), renderTreePosition.nextSibling());

        textRenderer->setInlineWrapperForDisplayContents(&displayContentsAnonymousWrapper);
        m_builder.attach(displayContentsAnonymousWrapper, WTFMove(textRenderer));
        return;
    }

    m_builder.attach(renderTreePosition.parent(), WTFMove(textRenderer), renderTreePosition.nextSibling());

    auto* textManipulationController = m_document->textManipulationControllerIfExists();
    if (textManipulationController) [[unlikely]]
        textManipulationController->didAddOrCreateRendererForNode(textNode);

    if (CheckedPtr cache = m_document->axObjectCache())
        cache->onRendererCreated(textNode);
}

void RenderTreeUpdater::updateTextRenderer(Text& text, const Style::TextUpdate* textUpdate, const ContainerNode* root)
{
    auto* existingRenderer = text.renderer();
    bool needsRenderer = textRendererIsNeeded(text);

    if (existingRenderer && textUpdate && textUpdate->inheritedDisplayContentsStyle) {
        if (existingRenderer->inlineWrapperForDisplayContents() || *textUpdate->inheritedDisplayContentsStyle) {
            // FIXME: We could update without teardown.
            tearDownTextRenderer(text, root, m_builder);
            existingRenderer = nullptr;
        }
    }

    if (existingRenderer) {
        if (needsRenderer) {
            if (textUpdate)
                existingRenderer->setTextWithOffset(text.data(), textUpdate->offset);
            return;
        }
        tearDownTextRenderer(text, root, m_builder);
        renderingParent().didCreateOrDestroyChildRenderer = true;
        return;
    }
    if (!needsRenderer)
        return;
    createTextRenderer(text, textUpdate);
    renderingParent().didCreateOrDestroyChildRenderer = true;
}

void RenderTreeUpdater::storePreviousRenderer(Node& node)
{
    auto* renderer = node.renderer();
    if (!renderer)
        return;
    ASSERT(renderingParent().previousChildRenderer != renderer);
    renderingParent().previousChildRenderer = renderer;
    if (renderer->isInFlow())
        renderingParent().hasPrecedingInFlowChild = true;
}

void RenderTreeUpdater::updateRenderViewStyle()
{
    if (m_styleUpdate->initialContainingBlockUpdate())
        m_document->renderView()->setStyle(RenderStyle::clone(*m_styleUpdate->initialContainingBlockUpdate()));
}

static void invalidateRebuildRootIfNeeded(Node& node)
{
    auto* ancestor = findRenderingAncestor(node);
    if (!ancestor)
        return;
    if (!RenderTreeBuilder::isRebuildRootForChildren(*ancestor->renderer()))
        return;
    ancestor->invalidateRenderer();
}

void RenderTreeUpdater::tearDownRenderers(Element& root, TeardownType teardownType)
{
    if (!root.renderer() && !root.hasDisplayContents())
        return;
    auto* view = root.document().renderView();
    if (!view)
        return;

    RenderTreeBuilder builder(*view);
    tearDownRenderers(root, teardownType, builder);
    invalidateRebuildRootIfNeeded(root);
}

void RenderTreeUpdater::tearDownRenderers(Element& root)
{
    tearDownRenderers(root, TeardownType::Full);
}

void RenderTreeUpdater::tearDownRenderersForShadowRootInsertion(Element& host)
{
    ASSERT(!host.shadowRoot());
    tearDownRenderers(host, TeardownType::FullAfterSlotOrShadowRootChange);
}

void RenderTreeUpdater::tearDownRenderersAfterSlotChange(Element& host)
{
    ASSERT(host.shadowRoot());
    tearDownRenderers(host, TeardownType::FullAfterSlotOrShadowRootChange);
}

void RenderTreeUpdater::tearDownRenderer(Text& text)
{
    auto* view = text.document().renderView();
    if (!view)
        return;

    RenderTreeBuilder builder(*view);
    tearDownTextRenderer(text, { }, builder);
    invalidateRebuildRootIfNeeded(text);
}

enum class DidRepaintAndMarkContainingBlock : bool { No, Yes };
static std::optional<DidRepaintAndMarkContainingBlock> repaintAndMarkContainingBlockDirtyBeforeTearDown(const Element& root, auto composedTreeDescendantsIterator)
{
    auto* destroyRootRenderer = root.renderer();
    if (destroyRootRenderer && destroyRootRenderer->renderTreeBeingDestroyed())
        return { };

    auto markContainingBlockDirty = [&](auto& renderer) {
        auto* container = renderer.container();
        if (!container) {
            ASSERT_NOT_REACHED();
            renderer.setNeedsLayout();
            return;
        }
        if (!renderer.isOutOfFlowPositioned()) {
            container->setChildNeedsLayout();
            container->setNeedsPreferredWidthsUpdate();
            return;
        }
        container->setNeedsLayoutForOverflowChange();
    };

    auto repaintBackdropIfApplicable = [&](auto& renderer) {
        if (auto backdropRenderer = renderer.backdropRenderer())
            backdropRenderer->repaint(RenderObject::ForceRepaint::Yes);
    };

    auto repaintRoot = [&](auto& renderer) {
        if (renderer.isBody()) {
            renderer.view().repaintRootContents();
            return;
        }
        // When repaint is propagated to our layer, we have to force it here on destroy as this layer will no be around to issue it _affter_ layout.
        auto* rendererLayerObject = dynamicDowncast<RenderLayerModelObject>(renderer);
        if (!rendererLayerObject || !rendererLayerObject->layer() || !rendererLayerObject->layer()->needsFullRepaint()) {
            renderer.repaint();
            return;
        }
        renderer.repaint(RenderObject::ForceRepaint::Yes);
    };

    if (destroyRootRenderer) {
        repaintRoot(*destroyRootRenderer);
        repaintBackdropIfApplicable(*destroyRootRenderer);
        markContainingBlockDirty(*destroyRootRenderer);
    }

    for (auto it = composedTreeDescendantsIterator.begin(), end = composedTreeDescendantsIterator.end(); it != end; ++it) {
        auto* renderer = it->renderer();
        if (!renderer)
            continue;

        // If child is the start or end of the selection, then clear the selection to
        // avoid problems of invalid pointers.
        if (renderer->isSelectionBorder())
            renderer->frame().selection().setNeedsSelectionUpdate();

        auto* renderElement = dynamicDowncast<RenderElement>(renderer);
        if (!renderElement)
            continue;

        auto shouldRepaint = [&] {
            if (!renderElement->everHadLayout())
                return false;
            if (renderElement->style().opacity().isTransparent())
                return false;
            if (renderElement->isOutOfFlowPositioned())
                return destroyRootRenderer != renderElement->containingBlock() || !destroyRootRenderer->hasNonVisibleOverflow();
            if (renderElement->isFloating() || renderElement->isPositioned())
                return !destroyRootRenderer || !destroyRootRenderer->hasNonVisibleOverflow();
            return false;
        };
        if (shouldRepaint())
            renderElement->repaint();
        repaintBackdropIfApplicable(*renderElement);
        if (renderElement->isOutOfFlowPositioned()) {
            // FIXME: Ideally we would check if containing block is the destory root or a descendent of the destroy root.
            markContainingBlockDirty(*renderElement);
        }
    }
    return destroyRootRenderer ? DidRepaintAndMarkContainingBlock::Yes : DidRepaintAndMarkContainingBlock::No;
}

void RenderTreeUpdater::tearDownRenderers(Element& root, TeardownType teardownType, RenderTreeBuilder& builder)
{
    Vector<Element*, 30> teardownStack;

    auto push = [&] (Element& element) {
        if (element.hasCustomStyleResolveCallbacks())
            element.willDetachRenderers();
        teardownStack.append(&element);
    };

    auto pop = [&] (unsigned depth) {
        while (teardownStack.size() > depth) {
            auto& element = *teardownStack.takeLast();
            auto styleable = Styleable::fromElement(element);

            // Make sure we don't leave any renderers behind in nodes outside the composed tree.
            // See ComposedTreeIterator::ComposedTreeIterator().
            if (is<HTMLSlotElement>(element) || element.shadowRoot())
                tearDownLeftoverChildrenOfComposedTree(element, builder);

            switch (teardownType) {
            case TeardownType::FullAfterSlotOrShadowRootChange:
                if (&element == &root) {
                    // Keep animations going on the host.
                    styleable.willChangeRenderer();
                    break;
                }
                element.clearHoverAndActiveStatusBeforeDetachingRenderer();
                break;
            case TeardownType::Full:
                styleable.cancelStyleOriginatedAnimations();
                element.clearHoverAndActiveStatusBeforeDetachingRenderer();
                break;
            case TeardownType::RendererUpdateCancelingAnimations:
                styleable.cancelStyleOriginatedAnimations();
                break;
            case TeardownType::RendererUpdate:
                styleable.willChangeRenderer();
                break;
            }

            GeneratedContent::removeBeforePseudoElement(element, builder);
            GeneratedContent::removeAfterPseudoElement(element, builder);

            if (!is<PseudoElement>(element)) {
                // ::before and ::after cannot have a ::marker pseudo-element addressable via
                // CSS selectors, and as such cannot possibly have animations on them. Additionally,
                // we cannot create a Styleable with a PseudoElement.
                if (auto* renderListItem = dynamicDowncast<RenderListItem>(element.renderer())) {
                    if (renderListItem->markerRenderer())
                        Styleable(element, Style::PseudoElementIdentifier { PseudoId::Marker }).cancelStyleOriginatedAnimations();
                }
            }

            if (auto* renderer = element.renderer()) {
                if (auto backdropRenderer = renderer->backdropRenderer())
                    builder.destroyAndCleanUpAnonymousWrappers(*backdropRenderer, { });
                builder.destroyAndCleanUpAnonymousWrappers(*renderer, root.renderer());
                element.setRenderer(nullptr);
            }

            if (element.hasCustomStyleResolveCallbacks())
                element.didDetachRenderers();
        }
    };

    push(root);

    auto descendants = composedTreeDescendants(root);
    auto didRepaintRoot = repaintAndMarkContainingBlockDirtyBeforeTearDown(root, descendants);
    auto needsDescendantRepaintAndLayout = !didRepaintRoot || *didRepaintRoot == DidRepaintAndMarkContainingBlock::Yes ? NeedsRepaintAndLayout::No : NeedsRepaintAndLayout::Yes;
    for (auto it = descendants.begin(), end = descendants.end(); it != end; ++it) {
        pop(it.depth());

        if (auto* text = dynamicDowncast<Text>(*it)) {
            tearDownTextRenderer(*text, &root, builder, needsDescendantRepaintAndLayout);
            continue;
        }

        push(downcast<Element>(*it));
    }

    pop(0);

    tearDownLeftoverPaginationRenderersIfNeeded(root, builder);
}

void RenderTreeUpdater::tearDownTextRenderer(Text& text, const ContainerNode* root, RenderTreeBuilder& builder, NeedsRepaintAndLayout needsRepaintAndLayout)
{
    auto* renderer = text.renderer();
    if (!renderer)
        return;
    if (needsRepaintAndLayout == NeedsRepaintAndLayout::Yes) {
        renderer->repaint();
        if (auto* parent = renderer->parent()) {
            parent->setChildNeedsLayout();
            parent->setNeedsPreferredWidthsUpdate();
        }
    }
    builder.destroyAndCleanUpAnonymousWrappers(*renderer, root ? root->renderer() : nullptr);
    text.setRenderer(nullptr);
}

void RenderTreeUpdater::tearDownLeftoverPaginationRenderersIfNeeded(Element& root, RenderTreeBuilder& builder)
{
    if (&root != root.document().documentElement())
        return;
    WeakPtr renderView = root.document().renderView();
    for (auto* child = renderView->firstChild(); child;) {
        auto* nextSibling = child->nextSibling();
        if (is<RenderMultiColumnFlow>(*child)) {
            ASSERT(renderView->multiColumnFlow());
            renderView->clearMultiColumnFlow();
            builder.destroyAndCleanUpAnonymousWrappers(*child, root.renderer());
        } else if (is<RenderMultiColumnSet>(*child))
            builder.destroyAndCleanUpAnonymousWrappers(*child, root.renderer());
        child = nextSibling;
    }
    ASSERT(!renderView->multiColumnFlow());
}

void RenderTreeUpdater::tearDownLeftoverChildrenOfComposedTree(Element& element, RenderTreeBuilder& builder)
{
    for (auto* child = element.firstChild(); child; child = child->nextSibling()) {
        if (!child->renderer())
            continue;
        if (auto* text = dynamicDowncast<Text>(*child)) {
            tearDownTextRenderer(*text, &element, builder, NeedsRepaintAndLayout::No);
            continue;
        }
        if (auto* element = dynamicDowncast<Element>(*child))
            tearDownRenderers(*element, TeardownType::Full, builder);
    }
}

RenderView& RenderTreeUpdater::renderView()
{
    return *m_document->renderView();
}

void RenderTreeUpdater::destroyAndCancelAnimationsForSubtree(RenderElement& renderer)
{
    auto styleable = Styleable::fromRenderer(renderer);
    if (styleable)
        styleable->cancelStyleOriginatedAnimations();

    for (auto& descendant : descendantsOfType<RenderElement>(renderer)) {
        auto styleable = Styleable::fromRenderer(descendant);
        if (styleable)
            styleable->cancelStyleOriginatedAnimations();
    }

    m_builder.destroy(renderer);
}

}
