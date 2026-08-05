// Top-level imports
import Foundation
import AppKit
#if USE_APPLE_INTERNAL_SDK
@_weakLinked @_spi(Private) @_spi(ForAppKitOnly) import SwiftUI
#else
import SwiftUI_SPI
#endif

// SDK frameworks pulled in transitively
import AudioToolbox
import Carbon
import CoreAudio
import CoreMedia
import CoreMIDI
import ImageCaptureCore
import NaturalLanguage
import Network
import PDFKit
import Quartz
import QuickLook
import QuickLookUI

// Modules upstream of WebKit
import JavaScriptCore
import JavaScriptCore_Private
import WebCore_Private
import WebGPU
import WebGPU_Private
import bmalloc
import wtf
