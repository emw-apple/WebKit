/*
 * Copyright (C) 2005 Oliver Hunt <ojh16@student.canterbury.ac.nz>
 * Copyright (C) 2022 Apple Inc. All rights reserved.
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

#pragma once

#include "SVGFELightElement.h"

namespace WebCore {

class SVGFEPointLightElement final : public SVGFELightElement {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(SVGFEPointLightElement);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(SVGFEPointLightElement);
public:
    static Ref<SVGFEPointLightElement> create(const QualifiedName&, Document&);

private:
    SVGFEPointLightElement(const QualifiedName&, Document&);

    Ref<LightSource> lightSource() const override;
};

static_assert(sizeof(SVGFEPointLightElement) == sizeof(SVGFELightElement));

} // namespace WebCore
