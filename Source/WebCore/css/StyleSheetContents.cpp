/*
 * (C) 1999-2003 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2004-2025 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "StyleSheetContents.h"

#include "CSSImportRule.h"
#include "CSSParser.h"
#include "CSSStyleSheet.h"
#include "CachePolicy.h"
#include "CachedCSSStyleSheet.h"
#include "CommonAtomStrings.h"
#include "DocumentInlines.h"
#include "FrameInlines.h"
#include "FrameLoader.h"
#include "LocalFrame.h"
#include "MediaList.h"
#include "Node.h"
#include "OriginAccessPatterns.h"
#include "Page.h"
#include "PageConsoleClient.h"
#include "ResourceLoadInfo.h"
#include "RuleSet.h"
#include "SecurityOrigin.h"
#include "StyleProperties.h"
#include "StyleRule.h"
#include "StyleRuleImport.h"
#include <wtf/Deque.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/Ref.h>
#include <wtf/text/MakeString.h>

#if ENABLE(CONTENT_EXTENSIONS)
#include "ContentRuleListResults.h"
#include "UserContentController.h"
#endif

namespace WebCore {

// Rough size estimate for the memory cache.
unsigned StyleSheetContents::estimatedSizeInBytes() const
{
    // Note that this does not take into account size of the strings hanging from various objects. 
    // The assumption is that nearly all of of them are atoms that would exist anyway.
    unsigned size = sizeof(*this);

    // FIXME: This ignores the children of media and region rules.
    // Most rules are StyleRules.
    size += ruleCount() * StyleRule::averageSizeInBytes();

    for (unsigned i = 0; i < m_importRules.size(); ++i) {
        if (StyleSheetContents* sheet = m_importRules[i]->styleSheet())
            size += sheet->estimatedSizeInBytes();
    }
    return size;
}

StyleSheetContents::StyleSheetContents(StyleRuleImport* ownerRule, const String& originalURL, const CSSParserContext& context)
    : m_ownerRule(ownerRule)
    , m_originalURL(originalURL)
    , m_defaultNamespace(starAtom())
    , m_isUserStyleSheet(ownerRule && ownerRule->parentStyleSheet() && ownerRule->parentStyleSheet()->isUserStyleSheet())
    , m_parserContext(context)
{
}

StyleSheetContents::StyleSheetContents(const StyleSheetContents& o)
    : m_originalURL(o.m_originalURL)
    , m_encodingFromCharsetRule(o.m_encodingFromCharsetRule)
    , m_layerRulesBeforeImportRules(o.m_layerRulesBeforeImportRules.size())
    , m_importRules(o.m_importRules.size())
    , m_namespaceRules(o.m_namespaceRules.size())
    , m_childRules(o.m_childRules.size())
    , m_namespaces(o.m_namespaces)
    , m_defaultNamespace(o.m_defaultNamespace)
    , m_isUserStyleSheet(o.m_isUserStyleSheet)
    , m_loadCompleted(true)
    , m_hasSyntacticallyValidCSSHeader(o.m_hasSyntacticallyValidCSSHeader)
    , m_usesStyleBasedEditability(o.m_usesStyleBasedEditability)
    , m_hasNestingRulesCache(o.m_hasNestingRulesCache)
    , m_parserContext(o.m_parserContext)
{
    ASSERT(o.isCacheable());

    // FIXME: Copy import rules.
    ASSERT(o.m_importRules.isEmpty());

    // FIXME: Copy namespace rules.
    ASSERT(o.m_namespaceRules.isEmpty());

    for (size_t i = 0; i < m_layerRulesBeforeImportRules.size(); ++i)
        m_layerRulesBeforeImportRules[i] = o.m_layerRulesBeforeImportRules[i]->copy();

    for (size_t i = 0; i < m_childRules.size(); ++i)
        m_childRules[i] = o.m_childRules[i]->copy();
}

StyleSheetContents::~StyleSheetContents()
{
    clearRules();
}

bool StyleSheetContents::isCacheable() const
{
    // FIXME: Support copying import rules.
    if (!m_importRules.isEmpty())
        return false;
    // FIXME: Support copying namespace rules.
    if (!m_namespaceRules.isEmpty())
        return false;
    // FIXME: Support cached stylesheets in import rules.
    if (m_ownerRule)
        return false;
    // This would require dealing with multiple clients for load callbacks.
    if (!m_loadCompleted)
        return false;
    if (m_didLoadErrorOccur)
        return false;
    // It is not the original sheet anymore.
    if (m_isMutable)
        return false;
    // If the header is valid we are not going to need to check the SecurityOrigin.
    // FIXME: Valid mime type avoids the check too.
    if (!m_hasSyntacticallyValidCSSHeader)
        return false;
    if (hasNestingRules())
        return false;
    return true;
}

bool StyleSheetContents::isCacheableWithNoBaseURLDependency() const
{
    if (!isCacheable())
        return false;
    if (mayDependOnBaseURL())
        return false;
    return true;
}

void StyleSheetContents::parserAppendRule(Ref<StyleRuleBase>&& rule)
{
    ASSERT(!rule->isCharsetRule());

    if (auto* layerRule = dynamicDowncast<StyleRuleLayer>(rule.get()); layerRule && m_importRules.isEmpty() && m_childRules.isEmpty() && m_namespaceRules.isEmpty()) {
        if (layerRule->isStatement()) {
            m_layerRulesBeforeImportRules.append(*layerRule);
            return;
        }
    }

    if (auto* importRule = dynamicDowncast<StyleRuleImport>(rule.get())) {
        // Parser enforces that @import rules come before anything else except @charset.
        ASSERT(m_childRules.isEmpty());
        m_importRules.append(*importRule);
        m_importRules.last()->setParentStyleSheet(this);
        m_importRules.last()->requestStyleSheet();
        return;
    }

    if (auto* namespaceRule = dynamicDowncast<StyleRuleNamespace>(rule.get())) {
        // Parser enforces that @namespace rules come before all rules other than
        // import/charset rules
        ASSERT(m_childRules.isEmpty());
        parserAddNamespace(namespaceRule->prefix(), namespaceRule->uri());
        m_namespaceRules.append(*namespaceRule);
        return;
    }

    // NOTE: The selector list has to fit into RuleData. <http://webkit.org/b/118369>
    auto ruleHasTooManySelectors = [](StyleRule& rule) {
        return rule.selectorList().componentCount() > Style::RuleData::maximumSelectorComponentCount;
    };

    if (auto* styleRule = dynamicDowncast<StyleRuleWithNesting>(rule.get()); styleRule && ruleHasTooManySelectors(*styleRule)) {
        // We don't support nested rules with too many selectors
        return;
    }

    if (auto* styleRule = dynamicDowncast<StyleRule>(rule.get()); styleRule && ruleHasTooManySelectors(*styleRule)) {
        // If we're adding a rule with a huge number of selectors, split it up into multiple rules
        m_childRules.appendVector(styleRule->splitIntoMultipleRulesWithMaximumSelectorComponentCount(Style::RuleData::maximumSelectorComponentCount));
        return;
    }

    m_childRules.append(WTFMove(rule));
}

StyleRuleBase* StyleSheetContents::ruleAt(unsigned index) const
{
    ASSERT_WITH_SECURITY_IMPLICATION(index < ruleCount());
    
    unsigned childVectorIndex = index;
    if (childVectorIndex < m_layerRulesBeforeImportRules.size())
        return m_layerRulesBeforeImportRules[childVectorIndex].ptr();

    childVectorIndex -= m_layerRulesBeforeImportRules.size();

    if (childVectorIndex < m_importRules.size())
        return m_importRules[childVectorIndex].ptr();

    childVectorIndex -= m_importRules.size();
    
    if (childVectorIndex < m_namespaceRules.size())
        return m_namespaceRules[childVectorIndex].ptr();
    
    childVectorIndex -= m_namespaceRules.size();
    
    return m_childRules[childVectorIndex].ptr();
}

unsigned StyleSheetContents::ruleCount() const
{
    unsigned result = 0;
    result += m_layerRulesBeforeImportRules.size();
    result += m_importRules.size();
    result += m_namespaceRules.size();
    result += m_childRules.size();
    return result;
}

void StyleSheetContents::clearCharsetRule()
{
    m_encodingFromCharsetRule = String();
}

void StyleSheetContents::clearRules()
{
    for (unsigned i = 0; i < m_importRules.size(); ++i) {
        ASSERT(m_importRules.at(i)->parentStyleSheet() == this);
        m_importRules[i]->clearParentStyleSheet();
    }
    m_layerRulesBeforeImportRules.clear();
    m_importRules.clear();
    m_namespaceRules.clear();
    m_childRules.clear();
    clearCharsetRule();
}

void StyleSheetContents::parserSetEncodingFromCharsetRule(const String& encoding)
{
    // Parser enforces that there is ever only one @charset.
    ASSERT(m_encodingFromCharsetRule.isNull());
    m_encodingFromCharsetRule = encoding; 
}

bool StyleSheetContents::wrapperInsertRule(Ref<StyleRuleBase>&& rule, unsigned index)
{
    ASSERT(m_isMutable);
    ASSERT_WITH_SECURITY_IMPLICATION(index <= ruleCount());
    // Parser::parseRule doesn't currently allow @charset so we don't need to deal with it.
    ASSERT(!rule->isCharsetRule());

    // Maybe the insert will be legal if we treat early layer statement rules as normal child rules?
    auto shouldMoveLayerRulesBeforeImportToNormalChildRules = [&] {
        if (index >= m_layerRulesBeforeImportRules.size())
            return false;
        if (!m_importRules.isEmpty() || !m_namespaceRules.isEmpty())
            return false;
        auto* layerRule = dynamicDowncast<StyleRuleLayer>(rule.get());
        bool isLayerStatement = layerRule && layerRule->isStatement();
        return !rule->isImportRule() && !rule->isNamespaceRule() && !isLayerStatement;
    };

    if (shouldMoveLayerRulesBeforeImportToNormalChildRules())
        m_childRules.insertVector(0, std::exchange(m_layerRulesBeforeImportRules, { }));

    unsigned childVectorIndex = index;
    if (childVectorIndex < m_layerRulesBeforeImportRules.size() || (childVectorIndex == m_layerRulesBeforeImportRules.size() && is<StyleRuleLayer>(rule))) {
        auto* layerRule = dynamicDowncast<StyleRuleLayer>(rule.get());
        if (!layerRule)
            return false;
        if (layerRule->isStatement()) {
            m_layerRulesBeforeImportRules.insert(childVectorIndex, *layerRule);
            return true;
        }
        if (childVectorIndex < m_layerRulesBeforeImportRules.size())
            return false;
    }
    childVectorIndex -= m_layerRulesBeforeImportRules.size();

    if (childVectorIndex < m_importRules.size() || (childVectorIndex == m_importRules.size() && rule->isImportRule())) {
        // Inserting non-import rule before @import is not allowed.
        auto* importRule = dynamicDowncast<StyleRuleImport>(rule.get());
        if (!importRule)
            return false;
        m_importRules.insert(childVectorIndex, *importRule);
        m_importRules[childVectorIndex]->setParentStyleSheet(this);
        m_importRules[childVectorIndex]->requestStyleSheet();
        // FIXME: Stylesheet doesn't actually change meaningfully before the imported sheets are loaded.
        return true;
    }
    // Inserting @import rule after a non-import rule is not allowed.
    if (is<StyleRuleImport>(rule))
        return false;
    childVectorIndex -= m_importRules.size();

    if (childVectorIndex < m_namespaceRules.size() || (childVectorIndex == m_namespaceRules.size() && rule->isNamespaceRule())) {
        // Inserting non-namespace rules other than import and layer statement rules before @namespace is
        // not allowed.
        auto* namespaceRule = dynamicDowncast<StyleRuleNamespace>(rule.get());
        if (!namespaceRule)
            return false;
        // Inserting @namespace rule when rules other than import/namespace/charset
        // are present is not allowed.
        if (!m_childRules.isEmpty() || !m_layerRulesBeforeImportRules.isEmpty())
            return false;

        m_namespaceRules.insert(index, *namespaceRule);
        
        // For now to be compatible with IE and Firefox if a namespace rule with the same
        // prefix is added, it overwrites previous ones.
        // FIXME: The eventual correct behavior would be to ensure that the last value in
        // the list wins.
        parserAddNamespace(namespaceRule->prefix(), namespaceRule->uri());
        return true;
    }
    if (is<StyleRuleNamespace>(rule))
        return false;
    childVectorIndex -= m_namespaceRules.size();

    // If the number of selectors would overflow RuleData, we drop the operation.
    auto* styleRule = dynamicDowncast<StyleRule>(rule.get());
    if (styleRule && styleRule->selectorList().componentCount() > Style::RuleData::maximumSelectorComponentCount)
        return false;

    m_childRules.insert(childVectorIndex, WTFMove(rule));
    return true;
}

bool StyleSheetContents::wrapperDeleteRule(unsigned index)
{
    ASSERT(m_isMutable);
    ASSERT_WITH_SECURITY_IMPLICATION(index < ruleCount());

    unsigned childVectorIndex = index;
    if (childVectorIndex < m_layerRulesBeforeImportRules.size()) {
        m_layerRulesBeforeImportRules.removeAt(childVectorIndex);
        return true;
    }
    childVectorIndex -= m_layerRulesBeforeImportRules.size();

    if (childVectorIndex < m_importRules.size()) {
        m_importRules[childVectorIndex]->cancelLoad();
        m_importRules[childVectorIndex]->clearParentStyleSheet();
        m_importRules.removeAt(childVectorIndex);
        return true;
    }
    childVectorIndex -= m_importRules.size();

    if (childVectorIndex < m_namespaceRules.size()) {
        // Deleting @namespace rule when list contains anything other than @import or @namespace rules is not allowed.
        if (!m_childRules.isEmpty())
            return false;
        m_namespaceRules.removeAt(childVectorIndex);
        return true;
    }
    childVectorIndex -= m_namespaceRules.size();

    m_childRules.removeAt(childVectorIndex);
    return true;
}

void StyleSheetContents::parserAddNamespace(const AtomString& prefix, const AtomString& uri)
{
    ASSERT(!uri.isNull());
    if (prefix.isNull()) {
        m_defaultNamespace = uri;
        return;
    }
    PrefixNamespaceURIMap::AddResult result = m_namespaces.add(prefix, uri);
    if (result.isNewEntry)
        return;
    result.iterator->value = uri;
}

const AtomString& StyleSheetContents::namespaceURIFromPrefix(const AtomString& prefix)
{
    PrefixNamespaceURIMap::const_iterator it = m_namespaces.find(prefix);
    if (it == m_namespaces.end())
        return nullAtom();
    return it->value;
}

bool StyleSheetContents::parseAuthorStyleSheet(const CachedCSSStyleSheet* cachedStyleSheet, const SecurityOrigin* securityOrigin)
{
    bool isSameOriginRequest = securityOrigin && securityOrigin->canRequest(baseURL(), OriginAccessPatternsForWebProcess::singleton());
    CachedCSSStyleSheet::MIMETypeCheckHint mimeTypeCheckHint = isStrictParserMode(m_parserContext.mode) || !isSameOriginRequest ? CachedCSSStyleSheet::MIMETypeCheckHint::Strict : CachedCSSStyleSheet::MIMETypeCheckHint::Lax;
    bool hasValidMIMEType = true;
    bool hasHTTPStatusOK = true;
    String sheetText = cachedStyleSheet->sheetText(mimeTypeCheckHint, &hasValidMIMEType, &hasHTTPStatusOK);

    if (!hasHTTPStatusOK) {
        ASSERT(sheetText.isNull());
        return false;
    }
    if (!hasValidMIMEType) {
        ASSERT(sheetText.isNull());
        if (auto* document = singleOwnerDocument()) {
            if (auto* page = document->page()) {
                if (isStrictParserMode(m_parserContext.mode))
                    page->console().addMessage(MessageSource::Security, MessageLevel::Error, makeString("Did not parse stylesheet at '"_s, cachedStyleSheet->url().stringCenterEllipsizedToLength(), "' because non CSS MIME types are not allowed in strict mode."_s));
                else if (!cachedStyleSheet->mimeTypeAllowedByNosniff())
                    page->console().addMessage(MessageSource::Security, MessageLevel::Error, makeString("Did not parse stylesheet at '"_s, cachedStyleSheet->url().stringCenterEllipsizedToLength(), "' because non CSS MIME types are not allowed when 'X-Content-Type-Options: nosniff' is given."_s));
                else
                    page->console().addMessage(MessageSource::Security, MessageLevel::Error, makeString("Did not parse stylesheet at '"_s, cachedStyleSheet->url().stringCenterEllipsizedToLength(), "' because non CSS MIME types are not allowed for cross-origin stylesheets."_s));
            }
        }
        return false;
    }

    CSSParser::parseStyleSheet(sheetText, parserContext(), *this);
    return true;
}

bool StyleSheetContents::parseString(const String& sheetText)
{
    CSSParser::parseStyleSheet(sheetText, parserContext(), *this);
    return true;
}

bool StyleSheetContents::isLoading() const
{
    for (unsigned i = 0; i < m_importRules.size(); ++i) {
        if (m_importRules[i]->isLoading())
            return true;
    }
    return false;
}

void StyleSheetContents::checkLoaded()
{
    if (isLoading())
        return;

    Ref<StyleSheetContents> protectedThis(*this);
    StyleSheetContents* parentSheet = parentStyleSheet();
    if (parentSheet) {
        parentSheet->checkLoaded();
        m_loadCompleted = true;
        return;
    }
    RefPtr<Node> ownerNode = singleOwnerNode();
    if (!ownerNode) {
        m_loadCompleted = true;
        return;
    }
    m_loadCompleted = ownerNode->sheetLoaded();
    if (m_loadCompleted)
        ownerNode->notifyLoadedSheetAndAllCriticalSubresources(m_didLoadErrorOccur);
}

void StyleSheetContents::notifyLoadedSheet(const CachedCSSStyleSheet* sheet)
{
    ASSERT(sheet);
    m_didLoadErrorOccur |= sheet->errorOccurred();
    m_didLoadErrorOccur |= !sheet->mimeTypeAllowedByNosniff();
}

void StyleSheetContents::startLoadingDynamicSheet()
{
    if (Node* owner = singleOwnerNode())
        owner->startLoadingDynamicSheet();
}

StyleSheetContents* StyleSheetContents::rootStyleSheet() const
{
    const StyleSheetContents* root = this;
    while (root->parentStyleSheet())
        root = root->parentStyleSheet();
    return const_cast<StyleSheetContents*>(root);
}

Node* StyleSheetContents::singleOwnerNode() const
{
    StyleSheetContents* root = rootStyleSheet();
    if (root->m_clients.isEmpty())
        return nullptr;
    ASSERT(root->m_clients.size() == 1);
    return root->m_clients[0]->ownerNode();
}

Document* StyleSheetContents::singleOwnerDocument() const
{
    Node* ownerNode = singleOwnerNode();
    return ownerNode ? &ownerNode->document() : nullptr;
}

static bool traverseRulesInVector(const Vector<Ref<StyleRuleBase>>& rules, NOESCAPE const Function<bool(const StyleRuleBase&)>& handler)
{
    for (auto& rule : rules) {
        if (handler(rule))
            return true;
        if (auto styleRuleWithNesting = dynamicDowncast<StyleRuleWithNesting>(rule.ptr())) {
            if (traverseRulesInVector(styleRuleWithNesting->nestedRules(), handler))
                return true;
        }
        auto* groupRule = dynamicDowncast<StyleRuleGroup>(rule.get());
        if (!groupRule)
            continue;
        if (traverseRulesInVector(groupRule->childRules(), handler))
            return true;
    }
    return false;
}

bool StyleSheetContents::traverseRules(NOESCAPE const Function<bool(const StyleRuleBase&)>& handler) const
{
    for (auto& importRule : m_importRules) {
        if (handler(importRule))
            return true;
        RefPtr importedStyleSheet = importRule->styleSheet();
        if (importedStyleSheet && importedStyleSheet->traverseRules(handler))
            return true;
    }
    return traverseRulesInVector(m_childRules, handler);
}

bool StyleSheetContents::hasNestingRules() const
{
    if (m_hasNestingRulesCache)
        return *m_hasNestingRulesCache;

    m_hasNestingRulesCache = traverseRulesInVector(m_childRules, [&] (const auto& rule) {
        if (rule.isStyleRuleWithNesting())
            return true;
        if (rule.isNestedDeclarationsRule())
            return true;
        return false;
    });

    return *m_hasNestingRulesCache;
}

bool StyleSheetContents::traverseSubresources(NOESCAPE const Function<bool(const CachedResource&)>& handler) const
{
    return traverseRules([&] (const StyleRuleBase& rule) {
        switch (rule.type()) {
        case StyleRuleType::Style:
            return uncheckedDowncast<StyleRule>(rule).properties().traverseSubresources(handler);
        case StyleRuleType::StyleWithNesting:
            return uncheckedDowncast<StyleRuleWithNesting>(rule).properties().traverseSubresources(handler);
        case StyleRuleType::NestedDeclarations:
            return uncheckedDowncast<StyleRuleNestedDeclarations>(rule).properties().traverseSubresources(handler);
        case StyleRuleType::FontFace:
            return uncheckedDowncast<StyleRuleFontFace>(rule).properties().traverseSubresources(handler);
        case StyleRuleType::Import:
            if (auto* cachedResource = uncheckedDowncast<StyleRuleImport>(rule).cachedCSSStyleSheet())
                return handler(*cachedResource);
            return false;
        case StyleRuleType::CounterStyle:
            return m_parserContext.counterStyleAtRuleImageSymbolsEnabled;
        case StyleRuleType::Media:
        case StyleRuleType::Page:
        case StyleRuleType::Keyframes:
        case StyleRuleType::Namespace:
        case StyleRuleType::Charset:
        case StyleRuleType::Keyframe:
        case StyleRuleType::Supports:
        case StyleRuleType::LayerBlock:
        case StyleRuleType::LayerStatement:
        case StyleRuleType::Container:
        case StyleRuleType::FontFeatureValues:
        case StyleRuleType::FontFeatureValuesBlock:
        case StyleRuleType::FontPaletteValues:
        case StyleRuleType::Margin:
        case StyleRuleType::Property:
        case StyleRuleType::Scope:
        case StyleRuleType::StartingStyle:
        case StyleRuleType::ViewTransition:
        case StyleRuleType::PositionTry:
            return false;
        };
        ASSERT_NOT_REACHED();
        return false;
    });
}

bool StyleSheetContents::subresourcesAllowReuse(CachePolicy cachePolicy, FrameLoader& loader) const
{
    bool hasFailedOrExpiredResources = traverseSubresources([cachePolicy, &loader](const CachedResource& resource) {
        if (resource.loadFailedOrCanceled())
            return true;
        // We can't revalidate subresources individually so don't use reuse the parsed sheet if they need revalidation.
        if (resource.makeRevalidationDecision(cachePolicy) != CachedResource::RevalidationDecision::No)
            return true;

#if ENABLE(CONTENT_EXTENSIONS)
        // If a cached subresource is blocked or made HTTPS by a content blocker, we cannot reuse the cached stylesheet.
        auto* page = loader.frame().page();
        auto* documentLoader = loader.documentLoader();
        if (page && documentLoader) {
            const auto& request = resource.resourceRequest();
            auto results = page->protectedUserContentProvider()->processContentRuleListsForLoad(*page, request.url(), ContentExtensions::toResourceType(resource.type(), resource.resourceRequest().requester(), loader.frame().isMainFrame()), *documentLoader);
            if (results.shouldBlock() || results.summary.madeHTTPS)
                return true;
        }
#else
        UNUSED_PARAM(loader);
#endif

        return false;
    });
    return !hasFailedOrExpiredResources;
}

bool StyleSheetContents::isLoadingSubresources() const
{
    return traverseSubresources([](const CachedResource& resource) {
        return resource.isLoading();
    });
}

bool StyleSheetContents::mayDependOnBaseURL() const
{
    return traverseRules([&](const StyleRuleBase& rule) -> bool {
        switch (rule.type()) {
        case StyleRuleType::Style:
            return uncheckedDowncast<StyleRule>(rule).properties().mayDependOnBaseURL();
        case StyleRuleType::StyleWithNesting:
            return uncheckedDowncast<StyleRuleWithNesting>(rule).properties().mayDependOnBaseURL();
        case StyleRuleType::NestedDeclarations:
            return uncheckedDowncast<StyleRule>(rule).properties().mayDependOnBaseURL();
        case StyleRuleType::FontFace:
            return uncheckedDowncast<StyleRuleFontFace>(rule).properties().mayDependOnBaseURL();
        case StyleRuleType::Import:
        case StyleRuleType::CounterStyle:
        case StyleRuleType::Media:
        case StyleRuleType::Page:
        case StyleRuleType::Keyframes:
        case StyleRuleType::Namespace:
        case StyleRuleType::Charset:
        case StyleRuleType::Keyframe:
        case StyleRuleType::Supports:
        case StyleRuleType::LayerBlock:
        case StyleRuleType::LayerStatement:
        case StyleRuleType::Container:
        case StyleRuleType::FontFeatureValues:
        case StyleRuleType::FontFeatureValuesBlock:
        case StyleRuleType::FontPaletteValues:
        case StyleRuleType::Margin:
        case StyleRuleType::Property:
        case StyleRuleType::Scope:
        case StyleRuleType::StartingStyle:
        case StyleRuleType::ViewTransition:
        case StyleRuleType::PositionTry:
            return false;
        };
        ASSERT_NOT_REACHED();
        return false;
    });
}

StyleSheetContents* StyleSheetContents::parentStyleSheet() const
{
    return m_ownerRule ? m_ownerRule->parentStyleSheet() : nullptr;
}

void StyleSheetContents::registerClient(CSSStyleSheet* sheet)
{
    ASSERT(!m_clients.contains(sheet));
    m_clients.append(sheet);
}

void StyleSheetContents::unregisterClient(CSSStyleSheet* sheet)
{
    bool removed = m_clients.removeFirst(sheet);
    ASSERT_UNUSED(removed, removed);
}

void StyleSheetContents::addedToMemoryCache()
{
    ASSERT(isCacheable());
    ++m_inMemoryCacheCount;
}

void StyleSheetContents::removedFromMemoryCache()
{
    ASSERT(m_inMemoryCacheCount);
    ASSERT(isCacheable());
    --m_inMemoryCacheCount;
}

void StyleSheetContents::shrinkToFit()
{
    m_importRules.shrinkToFit();
    m_childRules.shrinkToFit();
}

} // namespace WebCore
