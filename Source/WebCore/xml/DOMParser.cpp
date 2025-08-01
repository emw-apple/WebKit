/*
 *  Copyright (C) 2003, 2006, 2008 Apple Inc. All rights reserved.
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
#include "DOMParser.h"

#include "CommonAtomStrings.h"
#include "ExceptionOr.h"
#include "HTMLDocument.h"
#include "NodeInlines.h"
#include "SVGDocument.h"
#include "SecurityOriginPolicy.h"
#include "Settings.h"
#include "TrustedType.h"
#include "XMLDocument.h"

namespace WebCore {

inline DOMParser::DOMParser(Document& contextDocument)
    : m_contextDocument(contextDocument)
    , m_settings(contextDocument.settings())
{
}

DOMParser::~DOMParser() = default;

Ref<DOMParser> DOMParser::create(Document& contextDocument)
{
    return adoptRef(*new DOMParser(contextDocument));
}

ExceptionOr<Ref<Document>> DOMParser::parseFromString(Variant<RefPtr<TrustedHTML>, String>&& string, const AtomString& contentType)
{
    auto stringValueHolder = trustedTypeCompliantString(*protectedContextDocument()->protectedScriptExecutionContext().get(), WTFMove(string), "DOMParser parseFromString"_s);

    if (stringValueHolder.hasException())
        return stringValueHolder.releaseException();

    RefPtr<Document> document;
    if (contentType == textHTMLContentTypeAtom())
        document = HTMLDocument::create(nullptr, m_settings, URL { });
    else if (contentType == applicationXHTMLContentTypeAtom())
        document = XMLDocument::createXHTML(nullptr, m_settings, URL { });
    else if (contentType == imageSVGContentTypeAtom())
        document = SVGDocument::create(nullptr, m_settings, URL { });
    else if (contentType == textXMLContentTypeAtom() || contentType == applicationXMLContentTypeAtom()) {
        document = XMLDocument::create(nullptr, m_settings, URL { });
        document->overrideMIMEType(contentType);
    } else
        return Exception { ExceptionCode::TypeError };

    if (m_contextDocument)
        document->setContextDocument(*m_contextDocument.get());
    document->setMarkupUnsafe(stringValueHolder.releaseReturnValue(), { });
    if (m_contextDocument) {
        document->setURL(URL { m_contextDocument->url() });
        document->setSecurityOriginPolicy(m_contextDocument->securityOriginPolicy());
    }
    return document.releaseNonNull();
}

} // namespace WebCore
