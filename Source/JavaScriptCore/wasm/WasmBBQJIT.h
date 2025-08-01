/*
 * Copyright (C) 2019-2024 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(WEBASSEMBLY_BBQJIT)

#include "PCToCodeOriginMap.h"
#include "SimpleRegisterAllocator.h"
#include "WasmCallingConvention.h"
#include "WasmCompilationContext.h"
#include "WasmFunctionParser.h"
#include "WasmLimits.h"

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

namespace JSC { namespace Wasm {

namespace BBQJITImpl {

class BBQJIT {
public:
    using ErrorType = String;
    using PartialResult = Expected<void, ErrorType>;
    using Address = MacroAssembler::Address;
    using BaseIndex = MacroAssembler::BaseIndex;
    using Imm32 = MacroAssembler::Imm32;
    using Imm64 = MacroAssembler::Imm64;
    using TrustedImm32 = MacroAssembler::TrustedImm32;
    using TrustedImm64 = MacroAssembler::TrustedImm64;
    using TrustedImmPtr = MacroAssembler::TrustedImmPtr;
    using RelationalCondition = MacroAssembler::RelationalCondition;
    using ResultCondition = MacroAssembler::ResultCondition;
    using DoubleCondition = MacroAssembler::DoubleCondition;
    using StatusCondition = MacroAssembler::StatusCondition;
    using Jump = MacroAssembler::Jump;
    using JumpList = MacroAssembler::JumpList;
    using DataLabelPtr = MacroAssembler::DataLabelPtr;

    // Functions can have up to 1000000 instructions, so 32 bits is a sensible maximum number of stack items or locals.
    using LocalOrTempIndex = uint32_t;

    static constexpr unsigned LocalIndexBits = 21;
    static_assert(maxFunctionLocals < 1 << LocalIndexBits);

    static constexpr GPRReg wasmScratchGPR = GPRInfo::nonPreservedNonArgumentGPR0; // Scratch registers to hold temporaries in operations.
#if USE(JSVALUE32_64)
    static constexpr GPRReg wasmScratchGPR2 = GPRInfo::nonPreservedNonArgumentGPR1;
#else
    static constexpr GPRReg wasmScratchGPR2 = InvalidGPRReg;
#endif
    static constexpr FPRReg wasmScratchFPR = FPRInfo::nonPreservedNonArgumentFPR0;

#if CPU(X86_64)
    static constexpr GPRReg shiftRCX = X86Registers::ecx;
#else
    static constexpr GPRReg shiftRCX = InvalidGPRReg;
#endif

#if USE(JSVALUE64)
    static constexpr GPRReg wasmBaseMemoryPointer = GPRInfo::wasmBaseMemoryPointer;
    static constexpr GPRReg wasmBoundsCheckingSizeRegister = GPRInfo::wasmBoundsCheckingSizeRegister;
#else
    static constexpr GPRReg wasmBaseMemoryPointer = InvalidGPRReg;
    static constexpr GPRReg wasmBoundsCheckingSizeRegister = InvalidGPRReg;
#endif

public:
    struct Location {
        enum Kind : uint8_t {
            None = 0,
            Stack = 1,
            Gpr = 2,
            Fpr = 3,
            Global = 4,
            StackArgument = 5,
            Gpr2 = 6
        };

        Location()
            : m_kind(None)
        { }

        static Location none();

        static Location fromStack(int32_t stackOffset);

        static Location fromStackArgument(int32_t stackOffset);

        static Location fromGPR(GPRReg gpr);

        static Location fromGPR2(GPRReg hi, GPRReg lo);

        static Location fromFPR(FPRReg fpr);

        static Location fromGlobal(int32_t globalOffset);

        static Location fromArgumentLocation(ArgumentLocation argLocation, TypeKind type);

        bool isNone() const;

        bool isGPR() const;

        bool isGPR2() const;

        bool isFPR() const;

        bool isRegister() const;

        bool isStack() const;

        bool isStackArgument() const;

        bool isGlobal() const;

        bool isMemory() const;

        int32_t asStackOffset() const;

        Address asStackAddress() const;

        int32_t asGlobalOffset() const;

        Address asGlobalAddress() const;

        int32_t asStackArgumentOffset() const;

        Address asStackArgumentAddress() const;

        Address asAddress() const;

        GPRReg asGPR() const;
        FPRReg asFPR() const;
        Reg asReg() const { return isGPR() ? Reg(asGPR()) : Reg(asFPR()); }

        GPRReg asGPRlo() const;

        GPRReg asGPRhi() const;

        void dump(PrintStream& out) const;

        bool operator==(Location other) const;

        Kind kind() const;

    private:
        union {
            // It's useful to we be able to cram a location into a 4-byte space, so that
            // we can store them efficiently in ControlData.
            struct {
                unsigned m_kind : 3;
                int32_t m_offset : 29;
            };
            struct {
                Kind m_padGpr;
                GPRReg m_gpr;
            };
            struct {
                Kind m_padFpr;
                FPRReg m_fpr;
            };
            struct {
                Kind m_padGpr2;
                GPRReg m_gprhi, m_gprlo;
            };
        };
    };

    static_assert(sizeof(Location) == 4);

    static bool isValidValueTypeKind(TypeKind kind);

    static TypeKind pointerType();

    static bool isFloatingPointType(TypeKind type);

    static bool typeNeedsGPR2(TypeKind type);

public:
    static uint32_t sizeOfType(TypeKind type);

    static TypeKind toValueKind(TypeKind kind);

    class Value {
    public:
        // Represents the location in which this value is stored.
        enum Kind : uint8_t {
            None = 0,
            Const = 1,
            Temp = 2,
            Local = 3,
            Pinned = 4 // Used if we need to represent a Location as a Value, mostly in operation calls
        };

        ALWAYS_INLINE bool isNone() const
        {
            return m_kind == None;
        }

        ALWAYS_INLINE bool isTemp() const
        {
            return m_kind == Temp;
        }

        ALWAYS_INLINE bool isLocal() const
        {
            return m_kind == Local;
        }

        ALWAYS_INLINE bool isPinned() const
        {
            return m_kind == Pinned;
        }

        ALWAYS_INLINE Kind kind() const
        {
            return m_kind;
        }

        ALWAYS_INLINE int32_t asI32() const
        {
            ASSERT(m_kind == Const);
            return m_i32;
        }

        ALWAYS_INLINE int64_t asI64() const
        {
            ASSERT(m_kind == Const);
            return m_i64;
        }

        ALWAYS_INLINE float asF32() const
        {
            ASSERT(m_kind == Const);
            return m_f32;
        }

        ALWAYS_INLINE double asF64() const
        {
            ASSERT(m_kind == Const);
            return m_f64;
        }

        ALWAYS_INLINE EncodedJSValue asRef() const
        {
            ASSERT(m_kind == Const);
            return m_ref;
        }

        ALWAYS_INLINE LocalOrTempIndex asTemp() const
        {
            ASSERT(m_kind == Temp);
            return m_index;
        }

        ALWAYS_INLINE LocalOrTempIndex asLocal() const
        {
            ASSERT(m_kind == Local);
            return m_index;
        }

        ALWAYS_INLINE bool isConst() const
        {
            return m_kind == Const;
        }

        ALWAYS_INLINE Location asPinned() const
        {
            ASSERT(m_kind == Pinned);
            return m_pinned;
        }

        ALWAYS_INLINE static Value fromI32(int32_t immediate)
        {
            Value val;
            val.m_kind = Const;
            val.m_type = TypeKind::I32;
            val.m_i32 = immediate;
            return val;
        }

        ALWAYS_INLINE static Value fromI64(int64_t immediate)
        {
            Value val;
            val.m_kind = Const;
            val.m_type = TypeKind::I64;
            val.m_i64 = immediate;
            return val;
        }

        ALWAYS_INLINE static Value fromF32(float immediate)
        {
            Value val;
            val.m_kind = Const;
            val.m_type = TypeKind::F32;
            val.m_f32 = immediate;
            return val;
        }

        ALWAYS_INLINE static Value fromF64(double immediate)
        {
            Value val;
            val.m_kind = Const;
            val.m_type = TypeKind::F64;
            val.m_f64 = immediate;
            return val;
        }

        ALWAYS_INLINE static Value fromRef(TypeKind refType, EncodedJSValue ref)
        {
            Value val;
            val.m_kind = Const;
            val.m_type = toValueKind(refType);
            val.m_ref = ref;
            return val;
        }

        ALWAYS_INLINE static Value fromTemp(TypeKind type, LocalOrTempIndex temp)
        {
            Value val;
            val.m_kind = Temp;
            val.m_type = toValueKind(type);
            val.m_index = temp;
            return val;
        }

        ALWAYS_INLINE static Value fromLocal(TypeKind type, LocalOrTempIndex local)
        {
            Value val;
            val.m_kind = Local;
            val.m_type = toValueKind(type);
            val.m_index = local;
            return val;
        }

        ALWAYS_INLINE static Value pinned(TypeKind type, Location location)
        {
            Value val;
            val.m_kind = Pinned;
            val.m_type = toValueKind(type);
            val.m_pinned = location;
            return val;
        }

        ALWAYS_INLINE static Value none()
        {
            Value val;
            val.m_kind = None;
            return val;
        }

        ALWAYS_INLINE uint32_t size() const
        {
            return sizeOfType(m_type);
        }

        ALWAYS_INLINE bool isFloat() const
        {
            return isFloatingPointType(m_type);
        }

        ALWAYS_INLINE TypeKind type() const
        {
            ASSERT(isValidValueTypeKind(m_type));
            return m_type;
        }

        ALWAYS_INLINE Value()
            : m_kind(None)
        { }

        int32_t asI64hi() const;

        int32_t asI64lo() const;

        void dump(PrintStream& out) const;

    private:
        union {
            int32_t m_i32;
            struct {
                int32_t lo, hi;
            } m_i32_pair;
            int64_t m_i64;
            float m_f32;
            double m_f64;
            LocalOrTempIndex m_index;
            Location m_pinned;
            EncodedJSValue m_ref;
        };

        Kind m_kind;
        TypeKind m_type;
    };

public:
    struct RegisterBinding {
        enum Kind : uint8_t {
            None = 0,
            Local = 1,
            Temp = 2,
            Scratch = 3, // Denotes a register bound for use as a scratch, not as a local or temp's location.
        };

        static RegisterBinding none() { return RegisterBinding(); }
        static RegisterBinding fromValue(Value value);
        static RegisterBinding scratch();

        Value toValue() const;

        bool isNone() const { return m_kind == None; }
        bool isValid() const { return m_kind != None; }
        bool isScratch() const { return m_kind == Scratch; }

        bool operator==(RegisterBinding other) const;

        void dump(PrintStream& out) const;

        unsigned hash() const;

        TypeKind m_type { 0 };
        unsigned m_kind : 3 { None };
        unsigned m_index : LocalIndexBits { 0 };
    };

    // Register bank definitions for SimpleRegisterAllocator
    struct GPRBank {
        using JITBackend = BBQJIT;
        using Register = GPRReg;
        static constexpr Register invalidRegister = InvalidGPRReg;
        // FIXME: Make this more precise
        static constexpr unsigned numberOfRegisters = 32;
        static constexpr Width defaultWidth = widthForBytes(sizeof(CPURegister));
    };

    struct FPRBank {
        using JITBackend = BBQJIT;
        using Register = FPRReg;
        static constexpr Register invalidRegister = InvalidFPRReg;
        // FIXME: Make this more precise
        static constexpr unsigned numberOfRegisters = 32;
        static constexpr Width defaultWidth = Width128;
    };

    using SpillHint = uint32_t;

    void flush(GPRReg, const RegisterBinding&);
    void flush(FPRReg, const RegisterBinding&);

    using GPRAllocator = SimpleRegisterAllocator<GPRBank>;
    using FPRAllocator = SimpleRegisterAllocator<FPRBank>;

    // Tables mapping from each register to the current value bound to it. Used for slow paths.
    struct RegisterBindings {
        void dump(PrintStream& out) const;
        // FIXME: We should really compress this since it's copied by slow paths to know how to restore the correct state.
        GPRAllocator::RegisterBindings m_gprBindings { RegisterBinding::none() }; // Tables mapping from each register to the current value bound to it.
        FPRAllocator::RegisterBindings m_fprBindings { RegisterBinding::none() };
    };
    RegisterBindings copyBindings() { return { m_gprAllocator.copyBindings(), m_fprAllocator.copyBindings() }; }

    template<unsigned GPRs, unsigned FPRs>
    class ScratchScope {
        WTF_MAKE_NONCOPYABLE(ScratchScope);
        WTF_FORBID_HEAP_ALLOCATION;
    public:
        template<typename... Args>
        ScratchScope(BBQJIT& generator, Args... locationsToPreserve)
            : m_generator(generator)
        {
            initializedPreservedSet(locationsToPreserve...);
            for (JSC::Reg reg : m_preserved) {
                if (reg.isGPR())
                    preserveGPR(reg.gpr());
                else
                    preserveFPR(reg.fpr());
            }

            for (unsigned i = 0; i < GPRs; i ++) {
                m_tempGPRs[i] = m_generator.m_gprAllocator.allocate(m_generator, RegisterBinding::scratch(), std::nullopt);
                m_generator.m_gprAllocator.lock(m_tempGPRs[i]);
            }
            for (unsigned i = 0; i < FPRs; i ++) {
                m_tempFPRs[i] = m_generator.m_fprAllocator.allocate(m_generator, RegisterBinding::scratch(), std::nullopt);
                m_generator.m_fprAllocator.lock(m_tempFPRs[i]);
            }
        }

        ~ScratchScope()
        {
            unbindEarly();
        }

        void unbindEarly()
        {
            unbindScratches();
            unbindPreserved();
        }

        void unbindScratches()
        {
            if (m_unboundScratches)
                return;

            m_unboundScratches = true;
            for (unsigned i = 0; i < GPRs; i ++)
                unbindGPR(m_tempGPRs[i]);
            for (unsigned i = 0; i < FPRs; i ++)
                unbindFPR(m_tempFPRs[i]);
        }

        void unbindPreserved()
        {
            if (m_unboundPreserved)
                return;

            m_unboundPreserved = true;
            for (JSC::Reg reg : m_preserved) {
                if (reg.isGPR())
                    unbindGPR(reg.gpr());
                else
                    unbindFPR(reg.fpr());
            }
        }

        inline GPRReg gpr(unsigned i) const
        {
            ASSERT(i < GPRs);
            ASSERT(!m_unboundScratches);
            return m_tempGPRs[i];
        }

        inline FPRReg fpr(unsigned i) const
        {
            ASSERT(i < FPRs);
            ASSERT(!m_unboundScratches);
            return m_tempFPRs[i];
        }

    private:
        GPRReg preserveGPR(GPRReg reg)
        {
            if (!m_generator.validGPRs().contains(reg, IgnoreVectors))
                return reg;
            const RegisterBinding& binding = m_generator.bindingFor(reg);
            m_generator.m_gprAllocator.lock(reg);
            if (m_preserved.contains(reg, IgnoreVectors) && !binding.isNone()) {
                if (Options::verboseBBQJITAllocation()) [[unlikely]]
                    dataLogLn("BBQ\tPreserving GPR ", MacroAssembler::gprName(reg), " currently bound to ", binding);
                return reg; // If the register is already bound, we don't need to preserve it ourselves.
            }
            ASSERT(binding.isNone());
            m_generator.m_gprAllocator.bind(reg, RegisterBinding::scratch(), 0);
            if (Options::verboseBBQJITAllocation()) [[unlikely]]
                dataLogLn("BBQ\tPreserving scratch GPR ", MacroAssembler::gprName(reg));
            return reg;
        }

        FPRReg preserveFPR(FPRReg reg)
        {
            if (!m_generator.validFPRs().contains(reg, Width::Width128))
                return reg;
            const RegisterBinding& binding = m_generator.bindingFor(reg);
            m_generator.m_fprAllocator.lock(reg);
            if (m_preserved.contains(reg, Width::Width128) && !binding.isNone()) {
                if (Options::verboseBBQJITAllocation()) [[unlikely]]
                    dataLogLn("BBQ\tPreserving FPR ", MacroAssembler::fprName(reg), " currently bound to ", binding);
                return reg; // If the register is already bound, we don't need to preserve it ourselves.
            }
            ASSERT(binding.isNone());
            m_generator.m_fprAllocator.bind(reg, RegisterBinding::scratch(), 0);
            if (Options::verboseBBQJITAllocation()) [[unlikely]]
                dataLogLn("BBQ\tPreserving scratch FPR ", MacroAssembler::fprName(reg));
            return reg;
        }

        void unbindGPR(GPRReg reg)
        {
            if (!m_generator.validGPRs().contains(reg, IgnoreVectors))
                return;
            const RegisterBinding& binding = m_generator.bindingFor(reg);
            m_generator.m_gprAllocator.unlock(reg);
            if (Options::verboseBBQJITAllocation()) [[unlikely]]
                dataLogLn("BBQ\tReleasing GPR ", MacroAssembler::gprName(reg), " preserved? ", m_preserved.contains(reg, IgnoreVectors), " binding: ", binding);
            if (m_preserved.contains(reg, IgnoreVectors) && !binding.isScratch())
                return; // It's okay if the register isn't bound to a scratch if we meant to preserve it - maybe it was just already bound to something.
            ASSERT(binding.isScratch());
            m_generator.m_gprAllocator.unbind(reg);
        }

        void unbindFPR(FPRReg reg)
        {
            if (!m_generator.validFPRs().contains(reg, Width::Width128))
                return;
            const RegisterBinding& binding = m_generator.bindingFor(reg);
            m_generator.m_fprAllocator.unlock(reg);
            if (Options::verboseBBQJITAllocation()) [[unlikely]]
                dataLogLn("BBQ\tReleasing FPR ", MacroAssembler::fprName(reg), " preserved? ", m_preserved.contains(reg, Width::Width128), " binding: ", binding);
            if (m_preserved.contains(reg, Width::Width128) && !binding.isScratch())
                return; // It's okay if the register isn't bound to a scratch if we meant to preserve it - maybe it was just already bound to something.
            ASSERT(binding.isScratch());
            m_generator.m_fprAllocator.unbind(reg);
        }

        template<typename... Args>
        void initializedPreservedSet(Location location, Args... args)
        {
            if (location.isGPR())
                m_preserved.add(location.asGPR(), IgnoreVectors);
            else if (location.isFPR())
                m_preserved.add(location.asFPR(), Width::Width128);
            else if (location.isGPR2()) {
                m_preserved.add(location.asGPRlo(), IgnoreVectors);
                m_preserved.add(location.asGPRhi(), IgnoreVectors);
            }
            initializedPreservedSet(args...);
        }

        template<typename... Args>
        void initializedPreservedSet(RegisterSet registers, Args... args)
        {
            for (JSC::Reg reg : registers)
                initializedPreservedSet(reg);
            initializedPreservedSet(args...);
        }

        template<typename... Args>
        void initializedPreservedSet(JSC::Reg reg, Args... args)
        {
            if (reg.isGPR())
                m_preserved.add(reg.gpr(), IgnoreVectors);
            else
                m_preserved.add(reg.fpr(), Width::Width128);
            initializedPreservedSet(args...);
        }

        inline void initializedPreservedSet() { }

        BBQJIT& m_generator;
        GPRReg m_tempGPRs[GPRs];
        FPRReg m_tempFPRs[FPRs];
        RegisterSet m_preserved;
        bool m_unboundScratches { false };
        bool m_unboundPreserved { false };
    };

    struct ControlData {
        static bool isIf(const ControlData& control) { return control.blockType() == BlockType::If; }
        static bool isTry(const ControlData& control) { return control.blockType() == BlockType::Try; }
        static bool isAnyCatch(const ControlData& control) { return control.blockType() == BlockType::Catch; }
        static bool isCatch(const ControlData& control) { return isAnyCatch(control) && control.catchKind() == CatchKind::Catch; }
        static bool isTopLevel(const ControlData& control) { return control.blockType() == BlockType::TopLevel; }
        static bool isLoop(const ControlData& control) { return control.blockType() == BlockType::Loop; }
        static bool isBlock(const ControlData& control) { return control.blockType() == BlockType::Block; }

        ControlData()
            : m_enclosedHeight(0)
        { }

        ControlData(BBQJIT& generator, BlockType, BlockSignature, LocalOrTempIndex enclosedHeight, RegisterSet liveScratchGPRs, RegisterSet liveScratchFPRs);

        // Re-use the argument layout of another block (eg. else will re-use the argument/result locations from if)
        enum BranchCallingConventionReuseTag { UseBlockCallingConventionOfOtherBranch };
        ControlData(BranchCallingConventionReuseTag, BlockType blockType, ControlData& otherBranch)
            : m_signature(otherBranch.m_signature)
            , m_blockType(blockType)
            , m_argumentLocations(otherBranch.m_argumentLocations)
            , m_resultLocations(otherBranch.m_resultLocations)
            , m_enclosedHeight(otherBranch.m_enclosedHeight)
        {
        }

        // This function is intentionally not using implicitSlots since arguments and results should not include implicit slot.
        Location allocateArgumentOrResult(BBQJIT& generator, TypeKind type, unsigned i, RegisterSet& remainingGPRs, RegisterSet& remainingFPRs);

        template<typename Stack>
        void flushAtBlockBoundary(BBQJIT& generator, unsigned targetArity, Stack& expressionStack, bool endOfWasmBlock)
        {
            // First, we flush all locals that were allocated outside of their designated slots in this block.
            for (unsigned i = 0; i < expressionStack.size(); ++i) {
                if (expressionStack[i].value().isLocal())
                    m_touchedLocals.add(expressionStack[i].value().asLocal());
            }
            for (LocalOrTempIndex touchedLocal : m_touchedLocals) {
                Value value = Value::fromLocal(generator.m_localTypes[touchedLocal], touchedLocal);
                if (generator.locationOf(value).isRegister())
                    generator.flushValue(value);
            }

            // If we are a catch block, we need to flush the exception value, since it's not represented on the expression stack.
            if (isAnyCatch(*this)) {
                Value value = generator.exception(*this);
                if (!endOfWasmBlock)
                    generator.flushValue(value);
                else
                    generator.consume(value);
            }

            for (unsigned i = 0; i < expressionStack.size(); ++i) {
                Value& value = expressionStack[i].value();
                int resultIndex = static_cast<int>(i) - static_cast<int>(expressionStack.size() - targetArity);

                // Next, we turn all constants into temporaries, so they can be given persistent slots on the stack.
                // If this is the end of the enclosing wasm block, we know we won't need them again, so this can be skipped.
                if (value.isConst() && (resultIndex < 0 || !endOfWasmBlock)) {
                    Value constant = value;
                    value = Value::fromTemp(value.type(), static_cast<LocalOrTempIndex>(enclosedHeight() + implicitSlots() + i));
                    Location slot = generator.locationOf(value);
                    generator.emitMoveConst(constant, slot);
                }

                // Next, we flush or consume all the temp values on the stack.
                if (value.isTemp()) {
                    if (!endOfWasmBlock)
                        generator.flushValue(value);
                    else if (resultIndex < 0)
                        generator.consume(value);
                }
            }
        }

        template<typename Stack, size_t N>
        bool addExit(BBQJIT& generator, const Vector<Location, N>& targetLocations, Stack& expressionStack)
        {
            unsigned targetArity = targetLocations.size();

            if (!targetArity)
                return false;

            // We move all passed temporaries to the successor, in its argument slots.
            unsigned offset = expressionStack.size() - targetArity;

            Vector<Value, 8> resultValues;
            Vector<Location, 8> resultLocations;
            for (unsigned i = 0; i < targetArity; ++i) {
                resultValues.append(expressionStack[i + offset].value());
                resultLocations.append(targetLocations[i]);
            }
            generator.emitShuffle(resultValues, resultLocations);
            return true;
        }

        template<typename Stack>
        void finalizeBlock(BBQJIT& generator, unsigned targetArity, Stack& expressionStack, bool preserveArguments)
        {
            // Finally, as we are leaving the block, we convert any constants into temporaries on the stack, so we don't blindly assume they have
            // the same constant values in the successor.
            unsigned offset = expressionStack.size() - targetArity;
            for (unsigned i = 0; i < targetArity; ++i) {
                Value& value = expressionStack[i + offset].value();
                if (value.isConst()) {
                    Value constant = value;
                    value = Value::fromTemp(value.type(), static_cast<LocalOrTempIndex>(enclosedHeight() + implicitSlots() + i + offset));
                    if (preserveArguments)
                        generator.emitMoveConst(constant, generator.canonicalSlot(value));
                } else if (value.isTemp()) {
                    if (preserveArguments)
                        generator.flushValue(value);
                    else
                        generator.consume(value);
                }
            }
        }

        template<typename Stack>
        void flushAndSingleExit(BBQJIT& generator, ControlData& target, Stack& expressionStack, bool isChildBlock, bool endOfWasmBlock, bool unreachable = false)
        {
            // Helper to simplify the common case where we don't need to handle multiple exits.
            const auto& targetLocations = isChildBlock ? target.argumentLocations() : target.targetLocations();
            flushAtBlockBoundary(generator, targetLocations.size(), expressionStack, endOfWasmBlock);
            if (!unreachable)
                addExit(generator, targetLocations, expressionStack);
            finalizeBlock(generator, targetLocations.size(), expressionStack, false);
        }

        template<typename Stack>
        void startBlock(BBQJIT& generator, Stack& expressionStack)
        {
            ASSERT(expressionStack.size() >= m_argumentLocations.size());

            for (unsigned i = 0; i < m_argumentLocations.size(); ++i) {
                ASSERT(!expressionStack[i + expressionStack.size() - m_argumentLocations.size()].value().isConst());
                generator.bind(expressionStack[i].value(), m_argumentLocations[i]);
            }
        }

        template<typename Stack>
        void resumeBlock(BBQJIT& generator, const ControlData& predecessor, Stack& expressionStack)
        {
            ASSERT(expressionStack.size() >= predecessor.resultLocations().size());

            for (unsigned i = 0; i < predecessor.resultLocations().size(); ++i) {
                unsigned offset = expressionStack.size() - predecessor.resultLocations().size();
                // Intentionally not using implicitSlots since results should not include implicit slot.
                expressionStack[i + offset].value() = Value::fromTemp(expressionStack[i + offset].type().kind, predecessor.enclosedHeight() + i);
                generator.bind(expressionStack[i + offset].value(), predecessor.resultLocations()[i]);
            }
        }

        void convertIfToBlock();

        void convertLoopToBlock();

        void addBranch(Jump jump);

        void addLabel(Box<CCallHelpers::Label>&& label);

        void delegateJumpsTo(ControlData& delegateTarget);

        void linkJumps(MacroAssembler::AbstractMacroAssemblerType* masm);

        void linkJumpsTo(MacroAssembler::Label label, MacroAssembler::AbstractMacroAssemblerType* masm);

        void linkIfBranch(MacroAssembler::AbstractMacroAssemblerType* masm);

        void dump(PrintStream& out) const;

        LocalOrTempIndex enclosedHeight() const;

        unsigned implicitSlots() const;

        const Vector<Location, 2>& targetLocations() const;

        const Vector<Location, 2>& argumentLocations() const;

        const Vector<Location, 2>& resultLocations() const;

        BlockType blockType() const;
        BlockSignature signature() const;

        FunctionArgCount branchTargetArity() const;

        Type branchTargetType(unsigned i) const;

        Type argumentType(unsigned i) const;

        CatchKind catchKind() const;

        void setCatchKind(CatchKind catchKind);

        unsigned tryStart() const;

        unsigned tryEnd() const;

        unsigned tryCatchDepth() const;

        void setTryInfo(unsigned tryStart, unsigned tryEnd, unsigned tryCatchDepth);

        struct TryTableTarget {
            CatchKind type;
            uint32_t tag;
            const TypeDefinition* exceptionSignature;
            ControlRef target;
        };
        using TargetList = Vector<TryTableTarget>;

        void setTryTableTargets(TargetList&&);

        void setIfBranch(MacroAssembler::Jump branch);

        void setLoopLabel(MacroAssembler::Label label);

        const MacroAssembler::Label& loopLabel() const;

        void touch(LocalOrTempIndex local);

    private:
        friend class BBQJIT;

        void fillLabels(CCallHelpers::Label label);

        BlockSignature m_signature;
        BlockType m_blockType;
        CatchKind m_catchKind { CatchKind::Catch };
        Vector<Location, 2> m_argumentLocations; // List of input locations to write values into when entering this block.
        Vector<Location, 2> m_resultLocations; // List of result locations to write values into when exiting this block.
        JumpList m_branchList; // List of branch control info for branches targeting the end of this block.
        Vector<Box<CCallHelpers::Label>> m_labels; // List of labels filled.
        MacroAssembler::Label m_loopLabel;
        MacroAssembler::Jump m_ifBranch;
        LocalOrTempIndex m_enclosedHeight; // Height of enclosed expression stack, used as the base for all temporary locations.
        BitVector m_touchedLocals; // Number of locals allocated to registers in this block.
        unsigned m_tryStart { 0 };
        unsigned m_tryEnd { 0 };
        unsigned m_tryCatchDepth { 0 };
        Vector<TryTableTarget, 8> m_tryTableTargets;
    };

    friend struct ControlData;

    using ExpressionType = Value;
    using ControlType = ControlData;
    using CallType = CallLinkInfo::CallType;
    using ResultList = Vector<ExpressionType, 8>;
    using ArgumentList = Vector<ExpressionType, 8>;
    using ControlEntry = typename FunctionParserTypes<ControlType, ExpressionType, CallType>::ControlEntry;
    using TypedExpression = typename FunctionParserTypes<ControlType, ExpressionType, CallType>::TypedExpression;
    using Stack = FunctionParser<BBQJIT>::Stack;
    using ControlStack = FunctionParser<BBQJIT>::ControlStack;
    using CatchHandler = FunctionParser<BBQJIT>::CatchHandler;

    unsigned stackCheckSize() const { return alignedFrameSize(m_maxCalleeStackSize + m_frameSize); }

private:
    unsigned m_loggingIndent = 0;

    template<typename... Args>
    void logInstructionData(bool first, const Value& value, const Location& location, const Args&... args)
    {
        if (!first)
            dataLog(", ");

        dataLog(value);
        if (location.kind() != Location::None)
            dataLog(":", location);
        logInstructionData(false, args...);
    }

    template<typename... Args>
    void logInstructionData(bool first, const Value& value, const Args&... args)
    {
        if (!first)
            dataLog(", ");

        dataLog(value);
        if (!value.isConst() && !value.isPinned())
            dataLog(":", locationOf(value));
        logInstructionData(false, args...);
    }

    template<size_t N, typename OverflowHandler, typename... Args>
    void logInstructionData(bool first, const Vector<TypedExpression, N, OverflowHandler>& expressions, const Args&... args)
    {
        for (const TypedExpression& expression : expressions) {
            if (!first)
                dataLog(", ");

            const Value& value = expression.value();
            dataLog(value);
            if (!value.isConst() && !value.isPinned())
                dataLog(":", locationOf(value));
            first = false;
        }
        logInstructionData(false, args...);
    }

    template<size_t N, typename OverflowHandler, typename... Args>
    void logInstructionData(bool first, const Vector<Value, N, OverflowHandler>& values, const Args&... args)
    {
        for (const Value& value : values) {
            if (!first)
                dataLog(", ");

            dataLog(value);
            if (!value.isConst() && !value.isPinned())
                dataLog(":", locationOf(value));
            first = false;
        }
        logInstructionData(false, args...);
    }

    template<size_t N, typename OverflowHandler, typename... Args>
    void logInstructionData(bool first, const Vector<Location, N, OverflowHandler>& values, const Args&... args)
    {
        for (const Location& value : values) {
            if (!first)
                dataLog(", ");
            dataLog(value);
            first = false;
        }
        logInstructionData(false, args...);
    }

    inline void logInstructionData(bool)
    {
        dataLogLn();
    }

    template<typename... Data>
    ALWAYS_INLINE void logInstruction(const char* opcode, SIMDLaneOperation op, const Data&... data)
    {
        dataLog("BBQ\t");
        for (unsigned i = 0; i < m_loggingIndent; i ++)
            dataLog(" ");
        dataLog(opcode, SIMDLaneOperationDump(op), " ");
        logInstructionData(true, data...);
    }

    template<typename... Data>
    ALWAYS_INLINE void logInstruction(const char* opcode, const Data&... data)
    {
        dataLog("BBQ\t");
        for (unsigned i = 0; i < m_loggingIndent; i ++)
            dataLog(" ");
        dataLog(opcode, " ");
        logInstructionData(true, data...);
    }

    template<typename T>
    struct Result {
        T value;

        Result(const T& value_in)
            : value(value_in)
        { }
    };

    template<typename... Args>
    void logInstructionData(bool first, const char* const& literal, const Args&... args)
    {
        if (!first)
            dataLog(" ");

        dataLog(literal);
        if (!std::strcmp(literal, "=> "))
            logInstructionData(true, args...);
        else
            logInstructionData(false, args...);
    }

    template<typename T, typename... Args>
    void logInstructionData(bool first, const Result<T>& result, const Args&... args)
    {
        if (!first)
            dataLog(" ");

        dataLog("=> ");
        logInstructionData(true, result.value, args...);
    }

    template<typename T, typename... Args>
    void logInstructionData(bool first, const T& t, const Args&... args)
    {
        if (!first)
            dataLog(", ");
        dataLog(t);
        logInstructionData(false, args...);
    }

#define RESULT(...) Result { __VA_ARGS__ }
#define LOG_INSTRUCTION(...) do { if (Options::verboseBBQJITInstructions()) [[unlikely]] { logInstruction(__VA_ARGS__); } } while (false)
#define LOG_INDENT() do { if (Options::verboseBBQJITInstructions()) [[unlikely]] { m_loggingIndent += 2; } } while (false);
#define LOG_DEDENT() do { if (Options::verboseBBQJITInstructions()) [[unlikely]] { m_loggingIndent -= 2; } } while (false);

public:
    // FIXME: Support fused branch compare on 32-bit platforms.
    static constexpr bool shouldFuseBranchCompare = is64Bit();

    static constexpr bool tierSupportsSIMD = true;
    static constexpr bool validateFunctionBodySize = true;

    BBQJIT(CCallHelpers& jit, const TypeDefinition& signature, BBQCallee& callee, const FunctionData& function, FunctionCodeIndex functionIndex, const ModuleInformation& info, Vector<UnlinkedWasmToWasmCall>& unlinkedWasmToWasmCalls, MemoryMode mode, InternalFunction* compilation, std::optional<bool> hasExceptionHandlers, unsigned loopIndexForOSREntry);

    ALWAYS_INLINE static Value emptyExpression()
    {
        return Value::none();
    }

    void setParser(FunctionParser<BBQJIT>* parser);

    bool addArguments(const TypeDefinition& signature);

    Value addConstant(Type type, uint64_t value);

    PartialResult addDrop(Value value);

    PartialResult addLocal(Type type, uint32_t numberOfLocals);

    Value instanceValue();

    // Tables
    PartialResult WARN_UNUSED_RETURN addTableGet(unsigned tableIndex, Value index, Value& result);

    PartialResult WARN_UNUSED_RETURN addTableSet(unsigned tableIndex, Value index, Value value);

    PartialResult WARN_UNUSED_RETURN addTableInit(unsigned elementIndex, unsigned tableIndex, ExpressionType dstOffset, ExpressionType srcOffset, ExpressionType length);

    PartialResult WARN_UNUSED_RETURN addElemDrop(unsigned elementIndex);

    PartialResult WARN_UNUSED_RETURN addTableSize(unsigned tableIndex, Value& result);

    PartialResult WARN_UNUSED_RETURN addTableGrow(unsigned tableIndex, Value fill, Value delta, Value& result);

    PartialResult WARN_UNUSED_RETURN addTableFill(unsigned tableIndex, Value offset, Value fill, Value count);

    PartialResult WARN_UNUSED_RETURN addTableCopy(unsigned dstTableIndex, unsigned srcTableIndex, Value dstOffset, Value srcOffset, Value length);

    // Locals

    PartialResult WARN_UNUSED_RETURN getLocal(uint32_t localIndex, Value& result);

    PartialResult WARN_UNUSED_RETURN setLocal(uint32_t localIndex, Value value);

    PartialResult WARN_UNUSED_RETURN teeLocal(uint32_t localIndex, Value, Value& result);

    // Globals

    Value topValue(TypeKind type);

    Value exception(const ControlData& control);

    PartialResult WARN_UNUSED_RETURN getGlobal(uint32_t index, Value& result);

    void emitWriteBarrier(GPRReg cellGPR);
    void emitMutatorFence();

    PartialResult WARN_UNUSED_RETURN setGlobal(uint32_t index, Value value);

    // Memory

    inline Location emitCheckAndPreparePointer(Value pointer, uint32_t uoffset, uint32_t sizeOfOperation)
    {
        ScratchScope<1, 0> scratches(*this);
        Location pointerLocation;
        if (pointer.isConst()) {
            pointerLocation = Location::fromGPR(scratches.gpr(0));
            emitMoveConst(pointer, pointerLocation);
        } else
            pointerLocation = loadIfNecessary(pointer);
        ASSERT(pointerLocation.isGPR());

#if USE(JSVALUE32_64)
        ScratchScope<2, 0> globals(*this);
        GPRReg wasmBaseMemoryPointer = globals.gpr(0);
        GPRReg wasmBoundsCheckingSizeRegister = globals.gpr(1);
        loadWebAssemblyGlobalState(wasmBaseMemoryPointer, wasmBoundsCheckingSizeRegister);
#endif

        uint64_t boundary = static_cast<uint64_t>(sizeOfOperation) + uoffset - 1;
        switch (m_mode) {
        case MemoryMode::BoundsChecking: {
            // We're not using signal handling only when the memory is not shared.
            // Regardless of signaling, we must check that no memory access exceeds the current memory size.
            m_jit.zeroExtend32ToWord(pointerLocation.asGPR(), wasmScratchGPR);
            if (boundary)
                m_jit.addPtr(TrustedImmPtr(boundary), wasmScratchGPR);
            throwExceptionIf(ExceptionType::OutOfBoundsMemoryAccess, m_jit.branchPtr(RelationalCondition::AboveOrEqual, wasmScratchGPR, wasmBoundsCheckingSizeRegister));
            break;
        }

        case MemoryMode::Signaling: {
            // We've virtually mapped 4GiB+redzone for this memory. Only the user-allocated pages are addressable, contiguously in range [0, current],
            // and everything above is mapped PROT_NONE. We don't need to perform any explicit bounds check in the 4GiB range because WebAssembly register
            // memory accesses are 32-bit. However WebAssembly register + offset accesses perform the addition in 64-bit which can push an access above
            // the 32-bit limit (the offset is unsigned 32-bit). The redzone will catch most small offsets, and we'll explicitly bounds check any
            // register + large offset access. We don't think this will be generated frequently.
            //
            // We could check that register + large offset doesn't exceed 4GiB+redzone since that's technically the limit we need to avoid overflowing the
            // PROT_NONE region, but it's better if we use a smaller immediate because it can codegens better. We know that anything equal to or greater
            // than the declared 'maximum' will trap, so we can compare against that number. If there was no declared 'maximum' then we still know that
            // any access equal to or greater than 4GiB will trap, no need to add the redzone.
            if (uoffset >= Memory::fastMappedRedzoneBytes()) {
                uint64_t maximum = m_info.memory.maximum() ? m_info.memory.maximum().bytes() : std::numeric_limits<uint32_t>::max();
                m_jit.zeroExtend32ToWord(pointerLocation.asGPR(), wasmScratchGPR);
                if (boundary)
                    m_jit.addPtr(TrustedImmPtr(boundary), wasmScratchGPR);
                throwExceptionIf(ExceptionType::OutOfBoundsMemoryAccess, m_jit.branchPtr(RelationalCondition::AboveOrEqual, wasmScratchGPR, TrustedImmPtr(static_cast<int64_t>(maximum))));
            }
            break;
        }
        }

#if CPU(ARM64)
        m_jit.addZeroExtend64(wasmBaseMemoryPointer, pointerLocation.asGPR(), wasmScratchGPR);
#else
        m_jit.zeroExtend32ToWord(pointerLocation.asGPR(), wasmScratchGPR);
        m_jit.addPtr(wasmBaseMemoryPointer, wasmScratchGPR);
#endif

        consume(pointer);
        return Location::fromGPR(wasmScratchGPR);
    }

    template<typename Functor>
    auto emitCheckAndPrepareAndMaterializePointerApply(Value pointer, uint32_t uoffset, uint32_t sizeOfOperation, Functor&& functor) -> decltype(auto);

    static inline uint32_t sizeOfLoadOp(LoadOpType op)
    {
        switch (op) {
        case LoadOpType::I32Load8S:
        case LoadOpType::I32Load8U:
        case LoadOpType::I64Load8S:
        case LoadOpType::I64Load8U:
            return 1;
        case LoadOpType::I32Load16S:
        case LoadOpType::I64Load16S:
        case LoadOpType::I32Load16U:
        case LoadOpType::I64Load16U:
            return 2;
        case LoadOpType::I32Load:
        case LoadOpType::I64Load32S:
        case LoadOpType::I64Load32U:
        case LoadOpType::F32Load:
            return 4;
        case LoadOpType::I64Load:
        case LoadOpType::F64Load:
            return 8;
        }
        RELEASE_ASSERT_NOT_REACHED();
    }

    static inline TypeKind typeOfLoadOp(LoadOpType op)
    {
        switch (op) {
        case LoadOpType::I32Load8S:
        case LoadOpType::I32Load8U:
        case LoadOpType::I32Load16S:
        case LoadOpType::I32Load16U:
        case LoadOpType::I32Load:
            return TypeKind::I32;
        case LoadOpType::I64Load8S:
        case LoadOpType::I64Load8U:
        case LoadOpType::I64Load16S:
        case LoadOpType::I64Load16U:
        case LoadOpType::I64Load32S:
        case LoadOpType::I64Load32U:
        case LoadOpType::I64Load:
            return TypeKind::I64;
        case LoadOpType::F32Load:
            return TypeKind::F32;
        case LoadOpType::F64Load:
            return TypeKind::F64;
        }
        RELEASE_ASSERT_NOT_REACHED();
    }

    Address materializePointer(Location pointerLocation, uint32_t uoffset);

    constexpr static const char* LOAD_OP_NAMES[14] = {
        "I32Load", "I64Load", "F32Load", "F64Load",
        "I32Load8S", "I32Load8U", "I32Load16S", "I32Load16U",
        "I64Load8S", "I64Load8U", "I64Load16S", "I64Load16U", "I64Load32S", "I64Load32U"
    };

    PartialResult WARN_UNUSED_RETURN load(LoadOpType loadOp, Value pointer, Value& result, uint32_t uoffset);

    inline uint32_t sizeOfStoreOp(StoreOpType op)
    {
        switch (op) {
        case StoreOpType::I32Store8:
        case StoreOpType::I64Store8:
            return 1;
        case StoreOpType::I32Store16:
        case StoreOpType::I64Store16:
            return 2;
        case StoreOpType::I32Store:
        case StoreOpType::I64Store32:
        case StoreOpType::F32Store:
            return 4;
        case StoreOpType::I64Store:
        case StoreOpType::F64Store:
            return 8;
        }
        RELEASE_ASSERT_NOT_REACHED();
    }

    constexpr static const char* STORE_OP_NAMES[9] = {
        "I32Store", "I64Store", "F32Store", "F64Store",
        "I32Store8", "I32Store16",
        "I64Store8", "I64Store16", "I64Store32",
    };

    PartialResult WARN_UNUSED_RETURN store(StoreOpType storeOp, Value pointer, Value value, uint32_t uoffset);

    PartialResult WARN_UNUSED_RETURN addGrowMemory(Value delta, Value& result);

    PartialResult WARN_UNUSED_RETURN addCurrentMemory(Value& result);

    PartialResult WARN_UNUSED_RETURN addMemoryFill(Value dstAddress, Value targetValue, Value count);

    PartialResult WARN_UNUSED_RETURN addMemoryCopy(Value dstAddress, Value srcAddress, Value count);

    PartialResult WARN_UNUSED_RETURN addMemoryInit(unsigned dataSegmentIndex, Value dstAddress, Value srcAddress, Value length);

    PartialResult WARN_UNUSED_RETURN addDataDrop(unsigned dataSegmentIndex);

    // Atomics

    static inline Width accessWidth(ExtAtomicOpType op)
    {
        return static_cast<Width>(memoryLog2Alignment(op));
    }

    static inline uint32_t sizeOfAtomicOpMemoryAccess(ExtAtomicOpType op)
    {
        return bytesForWidth(accessWidth(op));
    }

    void emitSanitizeAtomicResult(ExtAtomicOpType op, TypeKind resultType, GPRReg source, GPRReg dest);
    void emitSanitizeAtomicResult(ExtAtomicOpType op, TypeKind resultType, GPRReg result);
    void emitSanitizeAtomicResult(ExtAtomicOpType op, TypeKind resultType, Location source, Location dest);
    void emitSanitizeAtomicOperand(ExtAtomicOpType op, TypeKind operandType, Location source, Location dest);

    Location emitMaterializeAtomicOperand(Value value);

    template<typename Functor>
    void emitAtomicOpGeneric(ExtAtomicOpType op, Address address, GPRReg oldGPR, GPRReg scratchGPR, const Functor& functor);

    template<typename Functor>
    void emitAtomicOpGeneric(ExtAtomicOpType op, Address address, Location old, Location cur, const Functor& functor);

    Value WARN_UNUSED_RETURN emitAtomicLoadOp(ExtAtomicOpType loadOp, Type valueType, Location pointer, uint32_t uoffset);

    PartialResult WARN_UNUSED_RETURN atomicLoad(ExtAtomicOpType loadOp, Type valueType, ExpressionType pointer, ExpressionType& result, uint32_t uoffset);

    void emitAtomicStoreOp(ExtAtomicOpType storeOp, Type, Location pointer, Value value, uint32_t uoffset);

    PartialResult WARN_UNUSED_RETURN atomicStore(ExtAtomicOpType storeOp, Type valueType, ExpressionType pointer, ExpressionType value, uint32_t uoffset);

    Value emitAtomicBinaryRMWOp(ExtAtomicOpType op, Type valueType, Location pointer, Value value, uint32_t uoffset);

    PartialResult WARN_UNUSED_RETURN atomicBinaryRMW(ExtAtomicOpType op, Type valueType, ExpressionType pointer, ExpressionType value, ExpressionType& result, uint32_t uoffset);

    Value WARN_UNUSED_RETURN emitAtomicCompareExchange(ExtAtomicOpType op, Type, Location pointer, Value expected, Value value, uint32_t uoffset);

    PartialResult WARN_UNUSED_RETURN atomicCompareExchange(ExtAtomicOpType op, Type valueType, ExpressionType pointer, ExpressionType expected, ExpressionType value, ExpressionType& result, uint32_t uoffset);

    PartialResult WARN_UNUSED_RETURN atomicWait(ExtAtomicOpType op, ExpressionType pointer, ExpressionType value, ExpressionType timeout, ExpressionType& result, uint32_t uoffset);

    PartialResult WARN_UNUSED_RETURN atomicNotify(ExtAtomicOpType op, ExpressionType pointer, ExpressionType count, ExpressionType& result, uint32_t uoffset);

    PartialResult WARN_UNUSED_RETURN atomicFence(ExtAtomicOpType, uint8_t);

    // Saturated truncation.

    struct FloatingPointRange {
        Value min, max;
        bool closedLowerEndpoint;
    };

    enum class TruncationKind {
        I32TruncF32S,
        I32TruncF32U,
        I64TruncF32S,
        I64TruncF32U,
        I32TruncF64S,
        I32TruncF64U,
        I64TruncF64S,
        I64TruncF64U
    };

    TruncationKind truncationKind(OpType truncationOp);

    TruncationKind truncationKind(Ext1OpType truncationOp);

    FloatingPointRange lookupTruncationRange(TruncationKind truncationKind);

    void truncInBounds(TruncationKind truncationKind, Location operandLocation, Location resultLocation, FPRReg scratch1FPR, FPRReg scratch2FPR);
    void truncInBounds(TruncationKind truncationKind, Location operandLocation, Value& result, Location resultLocation);

    PartialResult WARN_UNUSED_RETURN truncTrapping(OpType truncationOp, Value operand, Value& result, Type returnType, Type operandType);
    PartialResult WARN_UNUSED_RETURN truncSaturated(Ext1OpType truncationOp, Value operand, Value& result, Type returnType, Type operandType);


    // GC
    PartialResult WARN_UNUSED_RETURN addRefI31(ExpressionType value, ExpressionType& result);

    PartialResult WARN_UNUSED_RETURN addI31GetS(ExpressionType value, ExpressionType& result);

    PartialResult WARN_UNUSED_RETURN addI31GetU(ExpressionType value, ExpressionType& result);

    const Ref<TypeDefinition> getTypeDefinition(uint32_t typeIndex);

    // Given a type index, verify that it's an array type and return its expansion
    const ArrayType* getArrayTypeDefinition(uint32_t typeIndex);

    // Given a type index for an array signature, look it up, expand it and
    // return the element type
    StorageType getArrayElementType(uint32_t typeIndex);

    // This will replace the existing value with a new value. Note that if this is an F32 then the top bits may be garbage but that's ok for our current usage.
    Value marshallToI64(Value value);

    void emitAllocateGCArrayUninitialized(GPRReg result, uint32_t typeIndex, ExpressionType size, GPRReg scratchGPR, GPRReg scratchGPR2);
    PartialResult WARN_UNUSED_RETURN addArrayNew(uint32_t typeIndex, ExpressionType size, ExpressionType initValue, ExpressionType& result);
    PartialResult WARN_UNUSED_RETURN addArrayNewDefault(uint32_t typeIndex, ExpressionType size, ExpressionType& result);

    using ArraySegmentOperation = EncodedJSValue SYSV_ABI (&)(JSC::JSWebAssemblyInstance*, uint32_t, uint32_t, uint32_t, uint32_t);
    void pushArrayNewFromSegment(ArraySegmentOperation operation, uint32_t typeIndex, uint32_t segmentIndex, ExpressionType arraySize, ExpressionType offset, ExceptionType exceptionType, ExpressionType& result);

    PartialResult WARN_UNUSED_RETURN addArrayNewData(uint32_t typeIndex, uint32_t dataIndex, ExpressionType arraySize, ExpressionType offset, ExpressionType& result);

    PartialResult WARN_UNUSED_RETURN addArrayNewElem(uint32_t typeIndex, uint32_t elemSegmentIndex, ExpressionType arraySize, ExpressionType offset, ExpressionType& result);

    void emitArrayStoreElementUnchecked(StorageType elementType, GPRReg payloadGPR, Location index, Value value, bool preserveIndex = false);
    void emitArrayStoreElementUnchecked(StorageType elementType, GPRReg payloadGPR, Value index, Value value);
    void emitArraySetUnchecked(uint32_t typeIndex, Value arrayref, Value index, Value value);

    PartialResult WARN_UNUSED_RETURN addArrayNewFixed(uint32_t typeIndex, ArgumentList& args, ExpressionType& result);

    void emitArrayGetPayload(StorageType, GPRReg arrayGPR, GPRReg payloadGPR);

    PartialResult WARN_UNUSED_RETURN addArrayGet(ExtGCOpType arrayGetKind, uint32_t typeIndex, ExpressionType arrayref, ExpressionType index, ExpressionType& result);

    PartialResult WARN_UNUSED_RETURN addArraySet(uint32_t typeIndex, ExpressionType arrayref, ExpressionType index, ExpressionType value);

    PartialResult WARN_UNUSED_RETURN addArrayLen(ExpressionType arrayref, ExpressionType& result);

    PartialResult WARN_UNUSED_RETURN addArrayFill(uint32_t typeIndex, ExpressionType arrayref, ExpressionType offset, ExpressionType value, ExpressionType size);

    PartialResult WARN_UNUSED_RETURN addArrayCopy(uint32_t dstTypeIndex, ExpressionType dst, ExpressionType dstOffset, uint32_t srcTypeIndex, ExpressionType src, ExpressionType srcOffset, ExpressionType size);

    PartialResult WARN_UNUSED_RETURN addArrayInitElem(uint32_t dstTypeIndex, ExpressionType dst, ExpressionType dstOffset, uint32_t srcElementIndex, ExpressionType srcOffset, ExpressionType size);

    PartialResult WARN_UNUSED_RETURN addArrayInitData(uint32_t dstTypeIndex, ExpressionType dst, ExpressionType dstOffset, uint32_t srcDataIndex, ExpressionType srcOffset, ExpressionType size);

    // Returns true if a writeBarrier/mutatorFence is needed.
    bool WARN_UNUSED_RETURN emitStructSet(GPRReg structGPR, const StructType& structType, uint32_t fieldIndex, Value value);

    void emitAllocateGCStructUninitialized(GPRReg resultGPR, uint32_t typeIndex, GPRReg scratchGPR, GPRReg scratchGPR2);
    PartialResult WARN_UNUSED_RETURN addStructNewDefault(uint32_t typeIndex, ExpressionType& result);
    PartialResult WARN_UNUSED_RETURN addStructNew(uint32_t typeIndex, ArgumentList& args, Value& result);

    PartialResult WARN_UNUSED_RETURN addStructGet(ExtGCOpType structGetKind, Value structValue, const StructType& structType, uint32_t fieldIndex, Value& result);

    PartialResult WARN_UNUSED_RETURN addStructSet(Value structValue, const StructType& structType, uint32_t fieldIndex, Value value);

    PartialResult WARN_UNUSED_RETURN addRefTest(ExpressionType reference, bool allowNull, int32_t heapType, bool shouldNegate, ExpressionType& result);

    PartialResult WARN_UNUSED_RETURN addRefCast(ExpressionType reference, bool allowNull, int32_t heapType, ExpressionType& result);

    PartialResult WARN_UNUSED_RETURN addAnyConvertExtern(ExpressionType reference, ExpressionType& result);

    PartialResult WARN_UNUSED_RETURN addExternConvertAny(ExpressionType reference, ExpressionType& result);

    // Basic operators
    PartialResult WARN_UNUSED_RETURN addSelect(Value condition, Value lhs, Value rhs, Value& result);

    template<typename Fold, typename RegReg, typename RegImm>
    inline PartialResult binary(const char* opcode, TypeKind resultType, Value& lhs, Value& rhs, Value& result, Fold fold, RegReg regReg, RegImm regImm)
    {
        if (lhs.isConst() && rhs.isConst()) {
            result = fold(lhs, rhs);
            LOG_INSTRUCTION(opcode, lhs, rhs, RESULT(result));
            return { };
        }

        Location lhsLocation = Location::none(), rhsLocation = Location::none();

        // Ensure all non-constant parameters are loaded into registers.
        if (!lhs.isConst())
            lhsLocation = loadIfNecessary(lhs);
        if (!rhs.isConst())
            rhsLocation = loadIfNecessary(rhs);

        ASSERT(lhs.isConst() || lhsLocation.isRegister());
        ASSERT(rhs.isConst() || rhsLocation.isRegister());

        consume(lhs); // If either of our operands are temps, consume them and liberate any bound
        consume(rhs); // registers. This lets us reuse one of the registers for the output.

        Location toReuse = lhs.isConst() ? rhsLocation : lhsLocation; // Select the location to reuse, preferring lhs.

        result = topValue(resultType); // Result will be the new top of the stack.
        Location resultLocation = allocateWithHint(result, toReuse);
        ASSERT(resultLocation.isRegister());

        LOG_INSTRUCTION(opcode, lhs, lhsLocation, rhs, rhsLocation, RESULT(result));

        if (lhs.isConst() || rhs.isConst())
            regImm(lhs, lhsLocation, rhs, rhsLocation, resultLocation);
        else
            regReg(lhs, lhsLocation, rhs, rhsLocation, resultLocation);

        return { };
    }

    template<typename Fold, typename Reg>
    inline PartialResult unary(const char* opcode, TypeKind resultType, Value& operand, Value& result, Fold fold, Reg reg)
    {
        if (operand.isConst()) {
            result = fold(operand);
            LOG_INSTRUCTION(opcode, operand, RESULT(result));
            return { };
        }

        Location operandLocation = loadIfNecessary(operand);
        ASSERT(operandLocation.isRegister());

        consume(operand); // If our operand is a temp, consume it and liberate its register if it has one.

        result = topValue(resultType); // Result will be the new top of the stack.
        Location resultLocation = allocateWithHint(result, operandLocation); // Try to reuse the operand location.
        ASSERT(resultLocation.isRegister());

        LOG_INSTRUCTION(opcode, operand, operandLocation, RESULT(result));

        reg(operand, operandLocation, resultLocation);
        return { };
    }

    struct ImmHelpers {
        ALWAYS_INLINE static Value& imm(Value& lhs, Value& rhs)
        {
            return lhs.isConst() ? lhs : rhs;
        }

        ALWAYS_INLINE static Location& immLocation(Location& lhsLocation, Location& rhsLocation)
        {
            return lhsLocation.isRegister() ? rhsLocation : lhsLocation;
        }

        ALWAYS_INLINE static Value& reg(Value& lhs, Value& rhs)
        {
            return lhs.isConst() ? rhs : lhs;
        }

        ALWAYS_INLINE static Location& regLocation(Location& lhsLocation, Location& rhsLocation)
        {
            return lhsLocation.isRegister() ? lhsLocation : rhsLocation;
        }
    };

#define BLOCK(...) __VA_ARGS__

#define EMIT_BINARY(opcode, resultType, foldExpr, regRegStatement, regImmStatement) \
        return binary(opcode, resultType, lhs, rhs, result, \
            [&](Value& lhs, Value& rhs) -> Value { \
                UNUSED_PARAM(lhs); \
                UNUSED_PARAM(rhs); \
                return foldExpr; /* Lambda to be called for constant folding, i.e. when both operands are constants. */ \
            }, \
            [&](Value& lhs, Location lhsLocation, Value& rhs, Location rhsLocation, Location resultLocation) -> void { \
                UNUSED_PARAM(lhs); \
                UNUSED_PARAM(rhs); \
                UNUSED_PARAM(lhsLocation); \
                UNUSED_PARAM(rhsLocation); \
                UNUSED_PARAM(resultLocation); \
                regRegStatement /* Lambda to be called when both operands are registers. */ \
            }, \
            [&](Value& lhs, Location lhsLocation, Value& rhs, Location rhsLocation, Location resultLocation) -> void { \
                UNUSED_PARAM(lhs); \
                UNUSED_PARAM(rhs); \
                UNUSED_PARAM(lhsLocation); \
                UNUSED_PARAM(rhsLocation); \
                UNUSED_PARAM(resultLocation); \
                regImmStatement /* Lambda to be when one operand is a register and the other is a constant. */ \
            });

#define EMIT_UNARY(opcode, resultType, foldExpr, regStatement) \
        return unary(opcode, resultType, operand, result, \
            [&](Value& operand) -> Value { \
                UNUSED_PARAM(operand); \
                return foldExpr; /* Lambda to be called for constant folding, i.e. when both operands are constants. */ \
            }, \
            [&](Value& operand, Location operandLocation, Location resultLocation) -> void { \
                UNUSED_PARAM(operand); \
                UNUSED_PARAM(operandLocation); \
                UNUSED_PARAM(resultLocation); \
                regStatement /* Lambda to be called when both operands are registers. */ \
            });

    PartialResult WARN_UNUSED_RETURN addI32Add(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64Add(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32Add(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Add(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32Sub(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64Sub(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32Sub(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Sub(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32Mul(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64Mul(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32Mul(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Mul(Value lhs, Value rhs, Value& result);

    template<typename Func>
    void addLatePath(Func func);

    void emitThrowException(ExceptionType type);

    void throwExceptionIf(ExceptionType type, Jump jump);

    void emitThrowOnNullReference(ExceptionType type, Location ref);

    template<typename IntType, bool IsMod>
    void emitModOrDiv(Value& lhs, Location lhsLocation, Value& rhs, Location rhsLocation, Value& result, Location resultLocation);

    template<typename IntType>
    Value checkConstantDivision(const Value& lhs, const Value& rhs);

    PartialResult WARN_UNUSED_RETURN addI32DivS(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64DivS(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32DivU(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64DivU(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32RemS(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64RemS(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32RemU(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64RemU(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32Div(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Div(Value lhs, Value rhs, Value& result);

    enum class MinOrMax { Min, Max };

    template<MinOrMax IsMinOrMax, typename FloatType>
    void emitFloatingPointMinOrMax(FPRReg left, FPRReg right, FPRReg result);

    template<MinOrMax IsMinOrMax, typename FloatType>
    constexpr FloatType computeFloatingPointMinOrMax(FloatType left, FloatType right)
    {
        if (std::isnan(left))
            return left;
        if (std::isnan(right))
            return right;

        if constexpr (IsMinOrMax == MinOrMax::Min)
            return std::min<FloatType>(left, right);
        else
            return std::max<FloatType>(left, right);
    }

    PartialResult WARN_UNUSED_RETURN addF32Min(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Min(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32Max(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Max(Value lhs, Value rhs, Value& result);

    inline float floatCopySign(float lhs, float rhs)
    {
        uint32_t lhsAsInt32 = std::bit_cast<uint32_t>(lhs);
        uint32_t rhsAsInt32 = std::bit_cast<uint32_t>(rhs);
        lhsAsInt32 &= 0x7fffffffu;
        rhsAsInt32 &= 0x80000000u;
        lhsAsInt32 |= rhsAsInt32;
        return std::bit_cast<float>(lhsAsInt32);
    }

    inline double doubleCopySign(double lhs, double rhs)
    {
        uint64_t lhsAsInt64 = std::bit_cast<uint64_t>(lhs);
        uint64_t rhsAsInt64 = std::bit_cast<uint64_t>(rhs);
        lhsAsInt64 &= 0x7fffffffffffffffu;
        rhsAsInt64 &= 0x8000000000000000u;
        lhsAsInt64 |= rhsAsInt64;
        return std::bit_cast<double>(lhsAsInt64);
    }

    PartialResult WARN_UNUSED_RETURN addI32And(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64And(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32Xor(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64Xor(Value lhs, Value rhs, Value& result);


    PartialResult WARN_UNUSED_RETURN addI32Or(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64Or(Value lhs, Value rhs, Value& result);

    void moveShiftAmountIfNecessary(Location& rhsLocation);

    PartialResult WARN_UNUSED_RETURN addI32Shl(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64Shl(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32ShrS(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64ShrS(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32ShrU(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64ShrU(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32Rotl(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64Rotl(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32Rotr(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64Rotr(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32Clz(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64Clz(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32Ctz(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64Ctz(Value operand, Value& result);

    PartialResult emitCompareI32(const char* opcode, Value& lhs, Value& rhs, Value& result, RelationalCondition condition, bool (*comparator)(int32_t lhs, int32_t rhs));

    PartialResult emitCompareI64(const char* opcode, Value& lhs, Value& rhs, Value& result, RelationalCondition condition, bool (*comparator)(int64_t lhs, int64_t rhs));

    PartialResult WARN_UNUSED_RETURN addI32Eq(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64Eq(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32Ne(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64Ne(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32LtS(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64LtS(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32LeS(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64LeS(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32GtS(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64GtS(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32GeS(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64GeS(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32LtU(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64LtU(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32LeU(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64LeU(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32GtU(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64GtU(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32GeU(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64GeU(Value lhs, Value rhs, Value& result);

    PartialResult emitCompareF32(const char* opcode, Value& lhs, Value& rhs, Value& result, DoubleCondition condition, bool (*comparator)(float lhs, float rhs));

    PartialResult emitCompareF64(const char* opcode, Value& lhs, Value& rhs, Value& result, DoubleCondition condition, bool (*comparator)(double lhs, double rhs));

    PartialResult WARN_UNUSED_RETURN addF32Eq(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Eq(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32Ne(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Ne(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32Lt(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Lt(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32Le(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Le(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32Gt(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Gt(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32Ge(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Ge(Value lhs, Value rhs, Value& result);

    PartialResult addI32WrapI64(Value operand, Value& result);

    PartialResult addI32Extend8S(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32Extend16S(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64Extend8S(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64Extend16S(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64Extend32S(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64ExtendSI32(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64ExtendUI32(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32Eqz(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64Eqz(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32Popcnt(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64Popcnt(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32ReinterpretF32(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64ReinterpretF64(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32ReinterpretI32(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64ReinterpretI64(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32DemoteF64(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64PromoteF32(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32ConvertSI32(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32ConvertUI32(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32ConvertSI64(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32ConvertUI64(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64ConvertSI32(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64ConvertUI32(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64ConvertSI64(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64ConvertUI64(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32Copysign(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Copysign(Value lhs, Value rhs, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32Floor(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Floor(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32Ceil(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Ceil(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32Abs(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Abs(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32Sqrt(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Sqrt(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32Neg(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Neg(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32Nearest(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Nearest(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF32Trunc(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addF64Trunc(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32TruncSF32(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32TruncSF64(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32TruncUF32(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI32TruncUF64(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64TruncSF32(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64TruncSF64(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64TruncUF32(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addI64TruncUF64(Value operand, Value& result);

    // References

    PartialResult WARN_UNUSED_RETURN addRefIsNull(Value operand, Value& result);

    PartialResult WARN_UNUSED_RETURN addRefAsNonNull(Value value, Value& result);

    PartialResult WARN_UNUSED_RETURN addRefEq(Value ref0, Value ref1, Value& result);

    PartialResult WARN_UNUSED_RETURN addRefFunc(FunctionSpaceIndex index, Value& result);

    void emitEntryTierUpCheck();

    // Control flow
    ControlData WARN_UNUSED_RETURN addTopLevel(BlockSignature signature);

    bool hasLoops() const;

    MacroAssembler::Label addLoopOSREntrypoint();

    PartialResult WARN_UNUSED_RETURN addBlock(BlockSignature signature, Stack& enclosingStack, ControlType& result, Stack& newStack);

    B3::Type toB3Type(Type type);

    B3::Type toB3Type(TypeKind kind);

    B3::ValueRep toB3Rep(Location location);

    StackMap makeStackMap(const ControlData& data, Stack& enclosingStack);

    void emitLoopTierUpCheckAndOSREntryData(const ControlData&, Stack& enclosingStack, unsigned loopIndex);

    PartialResult WARN_UNUSED_RETURN addLoop(BlockSignature signature, Stack& enclosingStack, ControlType& result, Stack& newStack, uint32_t loopIndex);

    PartialResult WARN_UNUSED_RETURN addIf(Value condition, BlockSignature signature, Stack& enclosingStack, ControlData& result, Stack& newStack);

    PartialResult WARN_UNUSED_RETURN addElse(ControlData& data, Stack& expressionStack);

    PartialResult WARN_UNUSED_RETURN addElseToUnreachable(ControlData& data);

    PartialResult WARN_UNUSED_RETURN addTry(BlockSignature signature, Stack& enclosingStack, ControlType& result, Stack& newStack);
    PartialResult WARN_UNUSED_RETURN addTryTable(BlockSignature, Stack& enclosingStack, const Vector<CatchHandler>& targets, ControlType& result, Stack& newStack);

    void emitCatchPrologue();

    void emitCatchAllImpl(ControlData& dataCatch);

    void emitCatchImpl(ControlData& dataCatch, const TypeDefinition& exceptionSignature, ResultList& results);
    void emitCatchTableImpl(ControlData& entryData, ControlType::TryTableTarget&);

    PartialResult WARN_UNUSED_RETURN addCatch(unsigned exceptionIndex, const TypeDefinition& exceptionSignature, Stack& expressionStack, ControlType& data, ResultList& results);

    PartialResult WARN_UNUSED_RETURN addCatchToUnreachable(unsigned exceptionIndex, const TypeDefinition& exceptionSignature, ControlType& data, ResultList& results);

    PartialResult WARN_UNUSED_RETURN addCatchAll(Stack& expressionStack, ControlType& data);

    PartialResult WARN_UNUSED_RETURN addCatchAllToUnreachable(ControlType& data);

    PartialResult WARN_UNUSED_RETURN addDelegate(ControlType& target, ControlType& data);

    PartialResult WARN_UNUSED_RETURN addDelegateToUnreachable(ControlType& target, ControlType& data);

    PartialResult WARN_UNUSED_RETURN addThrow(unsigned exceptionIndex, ArgumentList& arguments, Stack&);

    PartialResult WARN_UNUSED_RETURN addRethrow(unsigned, ControlType& data);

    PartialResult WARN_UNUSED_RETURN addThrowRef(ExpressionType exception, Stack&);

    void prepareForExceptions();

    PartialResult WARN_UNUSED_RETURN addReturn(const ControlData& data, const Stack& returnValues);

    PartialResult WARN_UNUSED_RETURN addBranch(ControlData& target, Value condition, Stack& results);

    PartialResult WARN_UNUSED_RETURN addBranchNull(ControlData& data, ExpressionType reference, Stack& returnValues, bool shouldNegate, ExpressionType& result);

    PartialResult WARN_UNUSED_RETURN addBranchCast(ControlData& data, ExpressionType reference, Stack& returnValues, bool allowNull, int32_t heapType, bool shouldNegate);

    PartialResult WARN_UNUSED_RETURN addSwitch(Value condition, const Vector<ControlData*>& targets, ControlData& defaultTarget, Stack& results);

    PartialResult WARN_UNUSED_RETURN endBlock(ControlEntry& entry, Stack& stack);

    PartialResult WARN_UNUSED_RETURN addEndToUnreachable(ControlEntry& entry, Stack& stack, bool unreachable = true);

    int alignedFrameSize(int frameSize) const;

    PartialResult WARN_UNUSED_RETURN endTopLevel(BlockSignature, const Stack&);

    enum BranchFoldResult {
        BranchAlwaysTaken,
        BranchNeverTaken,
        BranchNotFolded
    };

    BranchFoldResult WARN_UNUSED_RETURN tryFoldFusedBranchCompare(OpType, ExpressionType);
    Jump WARN_UNUSED_RETURN emitFusedBranchCompareBranch(OpType, ExpressionType, Location);
    BranchFoldResult WARN_UNUSED_RETURN tryFoldFusedBranchCompare(OpType, ExpressionType, ExpressionType);
    Jump WARN_UNUSED_RETURN emitFusedBranchCompareBranch(OpType, ExpressionType, Location, ExpressionType, Location);

    PartialResult WARN_UNUSED_RETURN addFusedBranchCompare(OpType, ControlType& target, ExpressionType, Stack&);
    PartialResult WARN_UNUSED_RETURN addFusedBranchCompare(OpType, ControlType& target, ExpressionType, ExpressionType, Stack&);
    PartialResult WARN_UNUSED_RETURN addFusedIfCompare(OpType, ExpressionType, BlockSignature, Stack&, ControlType&, Stack&);
    PartialResult WARN_UNUSED_RETURN addFusedIfCompare(OpType, ExpressionType, ExpressionType, BlockSignature, Stack&, ControlType&, Stack&);

    // Flush a value to its canonical slot.
    void flushValue(Value value);

    void restoreWebAssemblyContextInstance();

    void restoreWebAssemblyGlobalState();

    void loadWebAssemblyGlobalState(GPRReg wasmBaseMemoryPointer, GPRReg wasmBoundsCheckingSizeRegister);

    void restoreWebAssemblyGlobalStateAfterWasmCall();

    void flushRegistersForException();

    void flushRegisters();

    template<size_t N>
    void saveValuesAcrossCallAndPassArguments(const Vector<Value, N>& arguments, const CallInformation& callInfo, const TypeDefinition& signature);

    void slowPathSpillBindings(const RegisterBindings& bindings);
    void slowPathRestoreBindings(const RegisterBindings&);
    void restoreValuesAfterCall(const CallInformation& callInfo);

    template<size_t N>
    void returnValuesFromCall(Vector<Value, N>& results, const FunctionSignature& functionType, const CallInformation& callInfo);

    template<typename Func, size_t N>
    void emitCCall(Func function, const Vector<Value, N>& arguments);

    template<typename Func, size_t N>
    void emitCCall(Func function, const Vector<Value, N>& arguments, Value& result);

    void emitTailCall(FunctionSpaceIndex functionIndex, const TypeDefinition& signature, ArgumentList& arguments);
    PartialResult WARN_UNUSED_RETURN addCall(FunctionSpaceIndex functionIndex, const TypeDefinition& signature, ArgumentList& arguments, ResultList& results, CallType = CallType::Call);

    void emitIndirectCall(const char* opcode, const Value& callee, GPRReg calleeInstance, GPRReg calleeCode, const TypeDefinition& signature, ArgumentList& arguments, ResultList& results);
    void emitIndirectTailCall(const char* opcode, const Value& callee, GPRReg calleeInstance, GPRReg calleeCode, const TypeDefinition& signature, ArgumentList& arguments);
    void addRTTSlowPathJump(TypeIndex, GPRReg);
    void emitSlowPathRTTCheck(MacroAssembler::Label, TypeIndex, GPRReg);

    PartialResult WARN_UNUSED_RETURN addCallIndirect(unsigned tableIndex, const TypeDefinition& originalSignature, ArgumentList& args, ResultList& results, CallType = CallType::Call);

    PartialResult WARN_UNUSED_RETURN addCallRef(const TypeDefinition& originalSignature, ArgumentList& args, ResultList& results, CallType = CallType::Call);

    PartialResult WARN_UNUSED_RETURN addUnreachable();

    PartialResult WARN_UNUSED_RETURN addCrash();

    ALWAYS_INLINE void willParseOpcode();

    ALWAYS_INLINE void willParseExtendedOpcode();

    ALWAYS_INLINE void didParseOpcode();

    // SIMD

    bool usesSIMD();

    void notifyFunctionUsesSIMD();

    PartialResult addSIMDLoad(ExpressionType, uint32_t, ExpressionType&);

    PartialResult addSIMDStore(ExpressionType, ExpressionType, uint32_t);

    PartialResult addSIMDSplat(SIMDLane, ExpressionType, ExpressionType&);

    PartialResult addSIMDShuffle(v128_t, ExpressionType, ExpressionType, ExpressionType&);

    PartialResult addSIMDShift(SIMDLaneOperation, SIMDInfo, ExpressionType, ExpressionType, ExpressionType&);

    PartialResult addSIMDExtmul(SIMDLaneOperation, SIMDInfo, ExpressionType, ExpressionType, ExpressionType&);

    PartialResult addSIMDLoadSplat(SIMDLaneOperation, ExpressionType, uint32_t, ExpressionType&);

    PartialResult addSIMDLoadLane(SIMDLaneOperation, ExpressionType, ExpressionType, uint32_t, uint8_t, ExpressionType&);

    PartialResult addSIMDStoreLane(SIMDLaneOperation, ExpressionType, ExpressionType, uint32_t, uint8_t);

    PartialResult addSIMDLoadExtend(SIMDLaneOperation, ExpressionType, uint32_t, ExpressionType&);

    PartialResult addSIMDLoadPad(SIMDLaneOperation, ExpressionType, uint32_t, ExpressionType&);

    void materializeVectorConstant(v128_t, Location);

    ExpressionType addConstant(v128_t);

    PartialResult addExtractLane(SIMDInfo, uint8_t, Value, Value&);

    PartialResult addReplaceLane(SIMDInfo, uint8_t, ExpressionType, ExpressionType, ExpressionType&);

    PartialResult addSIMDI_V(SIMDLaneOperation, SIMDInfo, ExpressionType, ExpressionType&);

    PartialResult addSIMDV_V(SIMDLaneOperation, SIMDInfo, ExpressionType, ExpressionType&);

    PartialResult addSIMDBitwiseSelect(ExpressionType, ExpressionType, ExpressionType, ExpressionType&);

    PartialResult addSIMDRelOp(SIMDLaneOperation, SIMDInfo, ExpressionType, ExpressionType, B3::Air::Arg, ExpressionType&);

    void emitVectorMul(SIMDInfo info, Location left, Location right, Location result);

    PartialResult WARN_UNUSED_RETURN fixupOutOfBoundsIndicesForSwizzle(Location a, Location b, Location result);

    PartialResult addSIMDV_VV(SIMDLaneOperation, SIMDInfo, ExpressionType, ExpressionType, ExpressionType&);

    PartialResult addSIMDRelaxedFMA(SIMDLaneOperation, SIMDInfo, ExpressionType, ExpressionType, ExpressionType, ExpressionType&);

    void dump(const ControlStack&, const Stack*);
    void didFinishParsingLocals();
    void didPopValueFromStack(ExpressionType, ASCIILiteral);

    void finalize();

    Vector<UnlinkedHandlerInfo>&& takeExceptionHandlers();
    FixedBitVector&& takeDirectCallees();
    Vector<CCallHelpers::Label>&& takeCatchEntrypoints();
    Box<PCToCodeOriginMapBuilder> takePCToCodeOriginMapBuilder();

    std::unique_ptr<BBQDisassembler> takeDisassembler();

private:
    static bool isScratch(Location loc);

    void emitStoreConst(Value constant, Location dst);
    void emitStoreConst(StorageType, Value constant, BaseIndex dst);
    void emitStoreConst(StorageType, Value constant, Address dst);

    void emitMoveConst(Value constant, Location loc);

    void emitStore(TypeKind type, Location src, Location dst);
    void emitStore(StorageType, Location src, BaseIndex dst);
    void emitStore(StorageType, Location src, Address dst);

    void emitStore(Value src, Location dst);

    void emitMoveMemory(TypeKind type, Location src, Location dst);
    void emitMoveMemory(StorageType, Location src, BaseIndex dst);
    void emitMoveMemory(StorageType, Location src, Address dst);

    void emitMoveMemory(Value src, Location dst);

    void emitMoveRegister(TypeKind type, Location src, Location dst);

    void emitMoveRegister(Value src, Location dst);

    void emitLoad(TypeKind type, Location src, Location dst);

    void emitLoad(Value src, Location dst);

    void emitMove(TypeKind type, Location src, Location dst);

    void emitMove(Value src, Location dst);
    void emitMove(StorageType, Value src, BaseIndex dst);
    void emitMove(StorageType, Value src, Address dst);

    using ShuffleStatus = CCallHelpers::ShuffleStatus;

    template<size_t N, typename OverflowHandler>
    void emitShuffleMove(Vector<Value, N, OverflowHandler>& srcVector, Vector<Location, N, OverflowHandler>& dstVector, Vector<ShuffleStatus, N, OverflowHandler>& statusVector, unsigned index);

    template<size_t N, typename OverflowHandler>
    void emitShuffle(Vector<Value, N, OverflowHandler>& srcVector, Vector<Location, N, OverflowHandler>& dstVector);

    ControlData& currentControlData();

    void setLRUKey(Location, LocalOrTempIndex key);
    void increaseLRUKey(Location);
    uint32_t nextLRUKey() { return ++m_lastUseTimestamp; }

    Location bind(Value value);

    Location allocate(Value value);

    Location allocateWithHint(Value value, Location hint);

    Location locationOfWithoutBinding(Value value);

    Location locationOf(Value value);

    Location loadIfNecessary(Value value);

    // This should generally be avoided if possible but sometimes you just *need* a value in a register.
    Location materializeToGPR(Value, std::optional<ScratchScope<1, 0>>&);

    void consume(Value value);

    Location bind(Value value, Location loc);

    void unbind(Value value, Location loc);

    void unbindAllRegisters();

    const RegisterBinding& bindingFor(JSC::Reg reg) { return reg.isGPR() ? m_gprAllocator.bindingFor(reg.gpr()) : m_fprAllocator.bindingFor(reg.fpr()); }
    RegisterSet validGPRs() const { return m_gprAllocator.validRegisters(); }
    RegisterSet validFPRs() const { return m_fprAllocator.validRegisters(); }

    // We use this to free up specific registers that might get clobbered by an instruction.
    void clobber(GPRReg gpr) { m_gprAllocator.clobber(*this, gpr); }
    void clobber(FPRReg fpr) { m_fprAllocator.clobber(*this, fpr); }
    void clobber(JSC::Reg reg) { reg.isGPR() ? clobber(reg.gpr()) : clobber(reg.fpr()); }

    Location canonicalSlot(Value value);

    Location allocateStack(Value value);

    constexpr static int tempSlotSize = 16; // Size of the stack slot for a stack temporary. Currently the size of the largest possible temporary (a v128).

    enum class ShiftI64HelperOp { Lshift, Urshift, Rshift };
    void shiftI64Helper(ShiftI64HelperOp op, Location lhsLocation, Location rhsLocation, Location resultLocation);

    enum class RotI64HelperOp { Left, Right };
    void rotI64Helper(RotI64HelperOp op, Location lhsLocation, Location rhsLocation, Location resultLocation);

    void compareI64Helper(RelationalCondition condition, Location lhsLocation, Location rhsLocation, Location resultLocation);

    void F64CopysignHelper(Location lhsLocation, Location rhsLocation, Location resultLocation);

    bool canTierUpToOMG() const;

    CCallHelpers& m_jit;
    BBQCallee& m_callee;
    const FunctionData& m_function;
    const FunctionSignature* m_functionSignature;
    FunctionCodeIndex m_functionIndex;
    const ModuleInformation& m_info;
    MemoryMode m_mode;
    Vector<UnlinkedWasmToWasmCall>& m_unlinkedWasmToWasmCalls;
    FixedBitVector m_directCallees;
    std::optional<bool> m_hasExceptionHandlers;
    FunctionParser<BBQJIT>* m_parser;
    Vector<uint32_t, 4> m_arguments;
    ControlData m_topLevel;
    unsigned m_loopIndexForOSREntry;
    Vector<unsigned> m_outerLoops;
    unsigned m_osrEntryScratchBufferSize { 1 };

    Vector<Location, 8> m_locals; // Vectors mapping local and temp indices to binding indices.
    Vector<Location, 8> m_temps;
    Vector<Location, 8> m_localSlots; // Persistent stack slots for local variables.
    Vector<TypeKind, 8> m_localTypes; // Types of all non-argument locals in this function.
    GPRAllocator m_gprAllocator; // SimpleRegisterAllocator for GPRs
    FPRAllocator m_fprAllocator; // SimpleRegisterAllocator for FPRs
    SpillHint m_lastUseTimestamp; // Monotonically increasing integer incrementing with each register use.
    Vector<Function<void(BBQJIT&, CCallHelpers&)>, 8> m_latePaths; // Late paths to emit after the rest of the function body.
    Vector<std::tuple<MacroAssembler::JumpList, MacroAssembler::Label, RegisterBindings, Function<void(BBQJIT&, CCallHelpers&)>>> m_slowPaths; // Like a late path but for when we need to make a CCall thus need to restore our state.

    // FIXME: All uses of this are to restore sp, so we should emit these as a patchable sub instruction rather than move.
    Vector<DataLabelPtr, 1> m_frameSizeLabels;
    int m_frameSize { 0 };
    int m_maxCalleeStackSize { 0 };
    int m_localStorage { 0 }; // Stack offset pointing to the local with the lowest address.
    bool m_usesSIMD { false }; // Whether the function we are compiling uses SIMD instructions or not.
    bool m_usesExceptions { false };
    Checked<unsigned> m_tryCatchDepth { 0 };
    Checked<unsigned> m_callSiteIndex { 0 };

    RegisterSet m_callerSaveGPRs;
    RegisterSet m_callerSaveFPRs;
    RegisterSet m_callerSaves;

    InternalFunction* m_compilation;

    std::array<JumpList, numberOfExceptionTypes> m_exceptions { };
    Vector<UnlinkedHandlerInfo> m_exceptionHandlers;
    Vector<CCallHelpers::Label> m_catchEntrypoints;

    PCToCodeOriginMapBuilder m_pcToCodeOriginMapBuilder;
    std::unique_ptr<BBQDisassembler> m_disassembler;

#if ASSERT_ENABLED
    Vector<Value, 8> m_justPoppedStack;
    OpType m_prevOpcode;
#endif
};

using LocalOrTempIndex = BBQJIT::LocalOrTempIndex;
using Location = BBQJIT::Location;
using Value = BBQJIT::Value;
using ExpressionType = BBQJIT::Value;
using RegisterBinding = BBQJIT::RegisterBinding;
using ControlData = BBQJIT::ControlData;
using PartialResult = BBQJIT::PartialResult;
using Address = BBQJIT::Address;
using TruncationKind = BBQJIT::TruncationKind;
using FloatingPointRange = BBQJIT::FloatingPointRange;
using MinOrMax = BBQJIT::MinOrMax;

} // namespace JSC::Wasm::BBQJITImpl

class BBQCallee;

using BBQJIT = BBQJITImpl::BBQJIT;
Expected<std::unique_ptr<InternalFunction>, String> parseAndCompileBBQ(CompilationContext&, BBQCallee&, const FunctionData&, const TypeDefinition&, Vector<UnlinkedWasmToWasmCall>&, const ModuleInformation&, MemoryMode, FunctionCodeIndex functionIndex, std::optional<bool> hasExceptionHandlers, unsigned);

} } // namespace JSC::Wasm

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

#endif // ENABLE(WEBASSEMBLY_BBQJIT)
