/*
 * Copyright (C) 2014-2023 Apple Inc. All rights reserved.
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

DECLARE_SYSTEM_HEADER

#import <CoreVideo/CoreVideo.h>
#import <QuartzCore/QuartzCore.h>
#import <pal/spi/cg/CoreGraphicsSPI.h>
#import <wtf/spi/cocoa/IOSurfaceSPI.h>

#if USE(APPLE_INTERNAL_SDK)

#import <QuartzCore/CABackingStore.h>
#import <QuartzCore/CAColorMatrix.h>
#import <QuartzCore/CARenderServer.h>

#ifdef __OBJC__

#import <QuartzCore/CAAnimationPrivate.h>
#import <QuartzCore/CAContext.h>
#import <QuartzCore/CALayerHost.h>
#import <QuartzCore/CALayerPrivate.h>
#import <QuartzCore/CAMediaTimingFunctionPrivate.h>
#import <QuartzCore/QuartzCorePrivate.h>

#if PLATFORM(MAC)
#import <QuartzCore/CARenderCG.h>
#endif

#if PLATFORM(IOS_FAMILY)
#import <QuartzCore/CADisplay.h>
#import <QuartzCore/CADisplayLinkPrivate.h>
#endif

#if ENABLE(ARKIT_INLINE_PREVIEW)
#import <QuartzCore/CAFenceHandle.h>
#endif

#endif // __OBJC__

#else

#ifdef __OBJC__
typedef struct _CARenderContext CARenderContext;

#if PLATFORM(IOS_FAMILY)
@interface CADisplay : NSObject
@end

@interface CADisplayLink ()
@property (readonly, nonatomic) CFTimeInterval maximumRefreshRate;
@property (readonly, nonatomic) CADisplay *display;
@end
#endif

#if ENABLE(ARKIT_INLINE_PREVIEW)
@interface CAFenceHandle : NSObject
@end

@interface CAFenceHandle ()
- (mach_port_t)copyPort;
- (void)invalidate;
@end
#endif

@interface CAContext : NSObject
@end

@interface CAContext ()
+ (NSArray *)allContexts;
+ (CAContext *)currentContext;
+ (CAContext *)localContext;
+ (CAContext *)remoteContextWithOptions:(NSDictionary *)dict;
#if PLATFORM(MAC)
+ (CAContext *)contextWithCGSConnection:(CGSConnectionID)cid options:(NSDictionary *)dict;
#endif
+ (id)objectForSlot:(uint32_t)name;
- (uint32_t)createImageSlot:(CGSize)size hasAlpha:(BOOL)flag;
- (void)deleteSlot:(uint32_t)name;
- (void)invalidate;
- (void)invalidateFences;
- (mach_port_t)createFencePort;
- (void)setFencePort:(mach_port_t)port;
- (void)setFencePort:(mach_port_t)port commitHandler:(void(^)(void))block;

#if PLATFORM(MAC)
+ (void)setAllowsCGSConnections:(BOOL)flag;
#endif

@property uint32_t displayMask;
#if PLATFORM(MAC)
@property uint64_t GPURegistryID;
@property uint32_t commitPriority;
#endif
@property (readonly) uint32_t contextId;
@property (strong) CALayer *layer;
@property CGColorSpaceRef colorSpace;
@property (readonly) CARenderContext* renderContext;

#if ENABLE(ARKIT_INLINE_PREVIEW_IOS)
-(BOOL)addFence:(CAFenceHandle *)handle;
#endif

@end

@interface CAPresentationModifierGroup : NSObject
+ (instancetype)groupWithCapacity:(NSUInteger)capacity;
- (void)flush;
- (void)flushWithTransaction;
@end

@interface CAPresentationModifier : NSObject
- (instancetype)initWithKeyPath:(NSString *)keyPath initialValue:(id)value additive:(BOOL)additive group:(CAPresentationModifierGroup *)group;
@property (strong) id value;
@end

@interface CALayer ()
- (CAContext *)context;
- (void)setContextId:(uint32_t)contextID;
- (CGSize)size;
- (void *)regionBeingDrawn;
- (void)reloadValueForKeyPath:(NSString *)keyPath;
- (void)addPresentationModifier:(CAPresentationModifier *)modifier;
- (void)removePresentationModifier:(CAPresentationModifier *)modifier;
@property BOOL allowsGroupBlending;
@property BOOL allowsHitTesting;
@property BOOL hitTestsContentsAlphaChannel;
@property BOOL canDrawConcurrently;
@property BOOL contentsOpaque;
@property BOOL hitTestsAsOpaque;
@property BOOL inheritsTiming;
@property BOOL needsLayoutOnGeometryChange;
@property BOOL shadowPathIsBounds;
@property BOOL continuousCorners;
@property CGFloat contentsEDRStrength;
#if HAVE(CORE_ANIMATION_SEPARATED_LAYERS)
@property (getter=isSeparated) BOOL separated;
#endif
@property BOOL toneMapToStandardDynamicRange;
#if HAVE(SPATIAL_TRACKING_LABEL)
typedef NS_ENUM(unsigned, CASeparatedState) {
    kCALayerSeparatedStateNone = 0,
    kCALayerSeparatedStateTracked,
    kCALayerSeparatedStateSeparated,
};
@property CASeparatedState separatedState;
#endif
@end

@interface CABackdropLayer : CALayer
@property BOOL windowServerAware;
@end

struct CAColorMatrix {
    float m11, m12, m13, m14, m15;
    float m21, m22, m23, m24, m25;
    float m31, m32, m33, m34, m35;
    float m41, m42, m43, m44, m45;
};
typedef struct CAColorMatrix CAColorMatrix;

@interface NSValue (CADetails)
+ (NSValue *)valueWithCAColorMatrix:(CAColorMatrix)t;
@end

@interface CAFilter : NSObject <NSCopying, NSMutableCopying, NSCoding>
@end

@interface CAFilter ()
+ (CAFilter *)filterWithType:(NSString *)type;
@property (copy) NSString *name;
@end

typedef enum {
    kCATransactionPhasePreLayout,
    kCATransactionPhasePreCommit,
    kCATransactionPhasePostCommit,
    kCATransactionPhaseNull = ~0u
} CATransactionPhase;

@interface CATransaction ()
+ (void)addCommitHandler:(void(^)(void))block forPhase:(CATransactionPhase)phase;
+ (void)activate;
+ (CATransactionPhase)currentPhase;
+ (void)synchronize;
+ (uint32_t)currentState;
@end

@interface CALayerHost : CALayer
@property uint32_t contextId;
@property BOOL inheritsSecurity;
@property BOOL preservesFlip;
@end

@interface CASpringAnimation (Private)
@property CGFloat velocity;
@end

@interface CAMediaTimingFunction ()
- (float)_solveForInput:(float)t;
@end

@interface CAPortalLayer : CALayer
@property (weak) CALayer *sourceLayer;
@property BOOL matchesPosition;
@property BOOL matchesTransform;
@end

@interface CARemoteEffect: NSObject <NSCopying, NSSecureCoding>
@end

@interface CARemoteEffectGroup : CARemoteEffect
+ (instancetype)groupWithEffects:(NSArray<CARemoteEffect *> *)effects;
@property (copy) NSString *groupName;
@property (getter=isMatched) BOOL matched;
@property (getter=isSource) BOOL source;
@property (copy, nonatomic) NSDictionary *userInfo;
@end

@interface CALayer (RemoteEffects)
@property (copy) NSArray<CARemoteEffect *> *remoteEffects;
@end

#if HAVE(CORE_ANIMATION_FRAME_RATE_RANGE)
typedef uint32_t CAHighFrameRateReason;

#define CAHighFrameRateReasonMake(component, code) \
    (((uint32_t)((component) & 0xffff) << 16) | ((code) & 0xffff))

@interface CAAnimation ()
@property CAHighFrameRateReason highFrameRateReason;
@end

@interface CADisplayLink ()
@property CAHighFrameRateReason highFrameRateReason;
@end
#endif // HAVE(CORE_ANIMATION_FRAME_RATE_RANGE)

#endif // __OBJC__

#endif

@interface CALayer ()
@property BOOL usesWebKitBehavior;
@property BOOL sortsSublayers;
@property CGRect contentsDirtyRect;
@end

@interface CATransaction ()
+ (void)setDisableImplicitTransactionMainThreadAssert:(BOOL)flag;
@end

WTF_EXTERN_C_BEGIN

// FIXME: Declare these functions even when USE(APPLE_INTERNAL_SDK) is true once we can fix <rdar://problem/26584828> in a better way.
#if !USE(APPLE_INTERNAL_SDK)
void CARenderServerCaptureLayerWithTransform(mach_port_t, uint32_t clientId, uint64_t layerId, uint32_t slotId, int32_t ox, int32_t oy, const CATransform3D*);
void CARenderServerRenderLayerWithTransform(mach_port_t server_port, uint32_t client_id, uint64_t layer_id, IOSurfaceRef, int32_t ox, int32_t oy, const CATransform3D*);
void CARenderServerRenderDisplayLayerWithTransformAndTimeOffset(mach_port_t, CFStringRef display_name, uint32_t client_id, uint64_t layer_id, IOSurfaceRef, int32_t ox, int32_t oy, const CATransform3D*, CFTimeInterval);
#endif // USE(APPLE_INTERNAL_SDK)

typedef struct _CAMachPort *CAMachPortRef;
CAMachPortRef CAMachPortCreate(mach_port_t);
mach_port_t CAMachPortGetPort(CAMachPortRef);
CFTypeID CAMachPortGetTypeID(void);

typedef struct _CAIOSurface *CAIOSurfaceRef;

void CABackingStoreCollectBlocking(void);

typedef struct _CARenderCGContext CARenderCGContext;
typedef struct _CARenderContext CARenderContext;
typedef struct _CARenderUpdate CARenderUpdate;
CARenderCGContext *CARenderCGNew(uint32_t feature_flags);
CARenderUpdate* CARenderUpdateBegin(void* buffer, size_t, CFTimeInterval, const CVTimeStamp*, uint32_t finished_seed, const CGRect* bounds);
bool CARenderServerStart();
mach_port_t CARenderServerGetPort();
mach_port_t CARenderServerGetServerPort(const char *name);
void CARenderCGDestroy(CARenderCGContext*);
void CARenderCGRender(CARenderCGContext*, CARenderUpdate*, CGContextRef);
void CARenderUpdateAddContext(CARenderUpdate*, CARenderContext*);
void CARenderUpdateAddRect(CARenderUpdate*, const CGRect*);
void CARenderUpdateFinish(CARenderUpdate*);
bool CASupportsFeature(uint64_t);

WTF_EXTERN_C_END

extern NSString * const kCAFilterColorInvert;
extern NSString * const kCAFilterColorMatrix;
extern NSString * const kCAFilterColorMonochrome;
extern NSString * const kCAFilterColorHueRotate;
extern NSString * const kCAFilterColorSaturate;
extern NSString * const kCAFilterGaussianBlur;
extern NSString * const kCAFilterPlusD;
extern NSString * const kCAFilterPlusL;
extern NSString * const kCAFilterVibrantColorMatrix;

extern NSString * const kCAFilterNormalBlendMode;
extern NSString * const kCAFilterMultiplyBlendMode;
extern NSString * const kCAFilterScreenBlendMode;
extern NSString * const kCAFilterOverlayBlendMode;
extern NSString * const kCAFilterDarkenBlendMode;
extern NSString * const kCAFilterLightenBlendMode;
extern NSString * const kCAFilterColorDodgeBlendMode;
extern NSString * const kCAFilterColorBurnBlendMode;
extern NSString * const kCAFilterSoftLightBlendMode;
extern NSString * const kCAFilterHardLightBlendMode;
extern NSString * const kCAFilterDifferenceBlendMode;
extern NSString * const kCAFilterExclusionBlendMode;
extern NSString * const kCAFilterHueBlendMode;
extern NSString * const kCAFilterSaturationBlendMode;
extern NSString * const kCAFilterColorBlendMode;
extern NSString * const kCAFilterLuminosityBlendMode;

extern NSString * const kCAFilterInputColorMatrix;

extern NSString * const kCAContextCIFilterBehavior;
extern NSString * const kCAContextDisplayName;
extern NSString * const kCAContextDisplayId;
extern NSString * const kCAContextIgnoresHitTest;
extern NSString * const kCAContextPortNumber;

#if PLATFORM(IOS_FAMILY)
extern NSString * const kCAContextSecure;
extern NSString * const kCAContentsFormatRGBA10XR;
#endif
