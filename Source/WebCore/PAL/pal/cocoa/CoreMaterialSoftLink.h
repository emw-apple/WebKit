/*
 * Copyright (C) 2024 Apple Inc. All rights reserved.
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

#if HAVE(CORE_MATERIAL)

#import <pal/spi/cocoa/CoreMaterialSPI.h>
#import <wtf/SoftLinking.h>
#import <wtf/cocoa/TypeCastsCocoa.h>

SOFT_LINK_FRAMEWORK_FOR_HEADER(PAL, CoreMaterial)
SOFT_LINK_CLASS_FOR_HEADER(PAL, MTMaterialLayer)

SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialRecipeNone, NSString *)

SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialRecipePlatformContentLight, NSString *)
SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialRecipePlatformChromeLight, NSString *)
SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialRecipePlatformContentThickLight, NSString *)
SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialRecipePlatformContentUltraThinLight, NSString *)
SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialRecipePlatformContentThinLight, NSString *)

SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialRecipePlatformContentDark, NSString *)
SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialRecipePlatformChromeDark, NSString *)
SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialRecipePlatformContentThickDark, NSString *)
SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialRecipePlatformContentUltraThinDark, NSString *)
SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialRecipePlatformContentThinDark, NSString *)

SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialVisualStyleCategoryStroke, NSString *)
SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialVisualStyleCategoryFill, NSString *)

SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialVisualStyleNone, NSString *)
SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialVisualStylePrimary, NSString *)
SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialVisualStyleSecondary, NSString *)
SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialVisualStyleTertiary, NSString *)
SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialVisualStyleQuaternary, NSString *)
SOFT_LINK_CONSTANT_FOR_HEADER(PAL, CoreMaterial, MTCoreMaterialVisualStyleSeparator, NSString *)

SOFT_LINK_FUNCTION_FOR_HEADER(PAL, CoreMaterial, MTVisualStylingCreateDictionaryRepresentation, NSDictionary *, (MTCoreMaterialRecipe recipe, MTCoreMaterialVisualStyleCategory category, MTCoreMaterialVisualStyle style, NSDictionary *options), (recipe, category, style, options))

SPECIALIZE_OBJC_TYPE_TRAITS(MTMaterialLayer, PAL::getMTMaterialLayerClass())

#endif // HAVE(CORE_MATERIAL)
