/*
 * Copyright (C) 2015-2019 Apple Inc. All rights reserved.
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
#include "DFGStoreBarrierInsertionPhase.h"

#if ENABLE(DFG_JIT)

#include "DFGAbstractInterpreterInlines.h"
#include "DFGBlockMapInlines.h"
#include "DFGClobberize.h"
#include "DFGDoesGC.h"
#include "DFGGraph.h"
#include "DFGInPlaceAbstractState.h"
#include "DFGInsertionSet.h"
#include "DFGPhase.h"
#include "JSCInlines.h"
#include "StructureID.h"
#include <wtf/CommaPrinter.h>
#include <wtf/HashSet.h>

namespace JSC { namespace DFG {

namespace {

namespace DFGStoreBarrierInsertionPhaseInternal {
static constexpr bool verbose = false;
}

enum class PhaseMode {
    // Does only a local analysis for store barrier insertion and assumes that pointers live
    // from predecessor blocks may need barriers. Assumes CPS conventions. Does not use AI for
    // eliminating store barriers, but does a best effort to eliminate barriers when you're
    // storing a non-cell value by using Node::result() and by looking at constants. The local
    // analysis is based on GC epochs, so it will eliminate a lot of locally redundant barriers.
    Fast,
        
    // Does a global analysis for store barrier insertion. Reuses the GC-epoch-based analysis
    // used by Fast, but adds a conservative merge rule for propagating information from one
    // block to the next. This will ensure for example that if a value V coming from multiple
    // predecessors in B didn't need any more barriers at the end of each predecessor (either
    // because it was the last allocated object in that predecessor or because it just had a
    // barrier executed), then until we hit another GC point in B, we won't need another barrier
    // on V. Uses AI for eliminating barriers when we know that the value being stored is not a
    // cell. Assumes SSA conventions.
    Global
};

template<PhaseMode mode>
class StoreBarrierInsertionPhase : public Phase {
public:
    StoreBarrierInsertionPhase(Graph& graph)
        : Phase(graph, mode == PhaseMode::Fast ? "fast store barrier insertion"_s : "global store barrier insertion"_s)
        , m_insertionSet(graph)
    {
    }
    
    bool run()
    {
        dataLogIf(DFGStoreBarrierInsertionPhaseInternal::verbose, "Starting store barrier insertion:\n", m_graph);
        
        switch (mode) {
        case PhaseMode::Fast: {
            DFG_ASSERT(m_graph, nullptr, m_graph.m_form != SSA);
            
            m_graph.clearEpochs();
            for (BasicBlock* block : m_graph.blocksInNaturalOrder())
                handleBlock(block);
            return true;
        }
            
        case PhaseMode::Global: {
            DFG_ASSERT(m_graph, nullptr, m_graph.m_form == SSA);

            m_state = makeUniqueWithoutFastMallocCheck<InPlaceAbstractState>(m_graph);
            m_interpreter = makeUniqueWithoutFastMallocCheck<AbstractInterpreter<InPlaceAbstractState>>(m_graph, *m_state);
            
            m_isConverged = false;
            
            // First run the analysis. Inside basic blocks we use an epoch-based analysis that
            // is very precise. At block boundaries, we just propagate which nodes may need a
            // barrier. This gives us a very nice bottom->top fixpoint: we start out assuming
            // that no node needs any barriers at block boundaries, and then we converge
            // towards believing that all nodes need barriers. "Needing a barrier" is like
            // saying that the node is in a past epoch. "Not needing a barrier" is like saying
            // that the node is in the current epoch.
            m_stateAtHead = makeUniqueWithoutFastMallocCheck<BlockMap<UncheckedKeyHashSet<Node*>>>(m_graph);
            m_stateAtTail = makeUniqueWithoutFastMallocCheck<BlockMap<UncheckedKeyHashSet<Node*>>>(m_graph);
            
            BlockList postOrder = m_graph.blocksInPostOrder();
            
            bool changed = true;
            while (changed) {
                changed = false;
                
                // Intentional backwards loop because we are using RPO.
                for (unsigned blockIndex = postOrder.size(); blockIndex--;) {
                    BasicBlock* block = postOrder[blockIndex];
                    
                    if (!handleBlock(block)) {
                        // If the block didn't finish, then it cannot affect the fixpoint.
                        continue;
                    }
                    
                    // Construct the state-at-tail based on the epochs of live nodes and the
                    // current epoch. We grow state-at-tail monotonically to ensure convergence.
                    bool thisBlockChanged = false;
                    for (NodeFlowProjection node : block->ssa->liveAtTail) {
                        if (node.kind() == NodeFlowProjection::Shadow)
                            continue;
                        if (node->epoch() != m_currentEpoch) {
                            // If the node is older than the current epoch, then we may need to
                            // run a barrier on it in the future. So, add it to the state.
                            thisBlockChanged |= m_stateAtTail->at(block).add(node.node()).isNewEntry;
                        }
                    }
                    
                    if (!thisBlockChanged) {
                        // This iteration didn't learn anything new about this block.
                        continue;
                    }
                    
                    // Changed things. Make sure that we loop one more time.
                    changed = true;
                    
                    for (BasicBlock* successor : block->successors()) {
                        for (Node* node : m_stateAtTail->at(block))
                            m_stateAtHead->at(successor).add(node);
                    }
                }
            }
            
            // Tell handleBlock() that it's time to actually insert barriers for real.
            m_isConverged = true;
            
            for (BasicBlock* block : m_graph.blocksInNaturalOrder())
                handleBlock(block);
            
            return true;
        } }
        
        RELEASE_ASSERT_NOT_REACHED();
        return false;
    }

private:
    bool handleBlock(BasicBlock* block)
    {
        if (DFGStoreBarrierInsertionPhaseInternal::verbose) {
            dataLogLn("Dealing with block ", pointerDump(block));
            dataLogLnIf(reallyInsertBarriers(), "    Really inserting barriers.");
        }
        
        m_currentEpoch = Epoch::first();
        
        if (mode == PhaseMode::Global) {
            if (!block->cfaHasVisited)
                return false;
            m_state->beginBasicBlock(block);
            
            for (NodeFlowProjection node : block->ssa->liveAtHead) {
                if (node.kind() == NodeFlowProjection::Shadow)
                    continue;
                if (m_stateAtHead->at(block).contains(node.node())) {
                    // If previous blocks tell us that this node may need a barrier in the
                    // future, then put it in the ancient primordial epoch. This forces us to
                    // emit a barrier on any possibly-cell store, regardless of the epoch of the
                    // stored value.
                    node->setEpoch(Epoch());
                } else {
                    // If previous blocks aren't requiring us to run a barrier on this node,
                    // then put it in the current epoch. This means that we will skip barriers
                    // on this node so long as we don't allocate. It also means that we won't
                    // run barriers on stores to on one such node into another such node. That's
                    // fine, because nodes would be excluded from the state set if at the tails
                    // of all predecessors they always had the current epoch.
                    node->setEpoch(m_currentEpoch);
                }
            }
        }

        bool result = true;

        UncheckedKeyHashMap<AbstractHeap, Node*> potentialStackEscapes;
        
        for (m_nodeIndex = 0; m_nodeIndex < block->size(); ++m_nodeIndex) {
            m_node = block->at(m_nodeIndex);
            
            if (DFGStoreBarrierInsertionPhaseInternal::verbose) {
                WTF::dataFile().atomically([&](auto&) {
                    dataLog(
                        "    ", m_currentEpoch, ": Looking at node ", m_node, " with children: ");
                    CommaPrinter comma;
                    m_graph.doToChildren(
                        m_node,
                        [&] (Edge edge) {
                            dataLog(comma, edge, " (", edge->epoch(), ")");
                        });
                    dataLogLn();
                });
            }
            
            if (mode == PhaseMode::Global) {
                // Execute edges separately because we don't want to insert barriers if the
                // operation doing the store does a check that ensures that the child is not
                // a cell.
                m_interpreter->startExecuting();
                m_interpreter->executeEdges(m_node);
            }
            
            switch (m_node->op()) {
            case PutByValDirect:
            case PutByVal:
            case PutByValAlias: {
                switch (m_node->arrayMode().modeForPut().type()) {
                case Array::Generic:
                case Array::Float16Array:
                case Array::BigInt64Array:
                case Array::BigUint64Array: {
                    Edge child1 = m_graph.varArgChild(m_node, 0);
                    if (!m_graph.m_slowPutByVal.contains(m_node) && (child1.useKind() == CellUse || child1.useKind() == KnownCellUse))
                        // FIXME: there are some cases where we can avoid a store barrier by considering the value https://bugs.webkit.org/show_bug.cgi?id=230377
                        considerBarrier(child1);
                    break;
                }
                case Array::Contiguous:
                case Array::ArrayStorage:
                case Array::SlowPutArrayStorage: {
                    Edge child1 = m_graph.varArgChild(m_node, 0);
                    Edge child3 = m_graph.varArgChild(m_node, 2);
                    considerBarrier(child1, child3);
                    break;
                }
                default:
                    break;
                }
                break;
            }
                
            case ArrayPush: {
                switch (m_node->arrayMode().type()) {
                case Array::Contiguous:
                case Array::ArrayStorage:
                case Array::SlowPutArrayStorage:
                case Array::ForceExit: {
                    unsigned elementOffset = 2;
                    unsigned elementCount = m_node->numChildren() - elementOffset;
                    Edge& arrayEdge = m_graph.varArgChild(m_node, 1);
                    for (unsigned i = 0; i < elementCount; ++i) {
                        Edge& element = m_graph.varArgChild(m_node, i + elementOffset);
                        considerBarrier(arrayEdge, element);
                    }
                    break;
                }
                default:
                    break;
                }
                break;
            }
                
            case PutPrivateName: {
                if (!m_graph.m_slowPutByVal.contains(m_node) && (m_node->child1().useKind() == CellUse || m_node->child1().useKind() == KnownCellUse))
                    // FIXME: there are some cases where we can avoid a store barrier by considering the value https://bugs.webkit.org/show_bug.cgi?id=230377
                    considerBarrier(m_node->child1());
                break;
            }

            case PutPrivateNameById: {
                // We emit IC code when we have a non-null cacheableIdentifier and we need to introduce a
                // barrier for it. On PutPrivateName, we perform store barrier during slow path execution.
                considerBarrier(m_node->child1());
                break;
            }

            case SetPrivateBrand:
            case PutById:
            case PutByIdFlush:
            case PutByIdDirect:
            case PutStructure:
            case PutByIdMegamorphic: {
                considerBarrier(m_node->child1());
                break;
            }

            case DeleteById:
            case DeleteByVal: {
                // If child1 is not a cell-speculated, we call a generic implementation which emits write-barrier in C++ side.
                // FIXME: We should consider accept base:UntypedUse.
                // https://bugs.webkit.org/show_bug.cgi?id=209396
                if (isCell(m_node->child1().useKind()))
                    considerBarrier(m_node->child1());
                break;
            }

            case RegExpTestInline: {
                considerBarrier(m_node->child1());
                break;
            }

            case RecordRegExpCachedResult: {
                considerBarrier(m_graph.varArgChild(m_node, 0));
                break;
            }

            case PutClosureVar:
            case PutToArguments:
            case SetRegExpObjectLastIndex:
            case PutInternalField: {
                considerBarrier(m_node->child1(), m_node->child2());
                break;
            }

            case EnumeratorPutByVal:
            case PutByValMegamorphic: {
                Edge child1 = m_graph.varArgChild(m_node, 0);
                considerBarrier(child1);
                break;
            }
                
            case MultiPutByOffset:
            case MultiDeleteByOffset: {
                // These nodes may cause transition too.
                considerBarrier(m_node->child1());
                break;
            }
                
            case PutByOffset: {
                considerBarrier(m_node->child2(), m_node->child3());
                break;
            }
                
            case PutGlobalVariable: {
                considerBarrier(m_node->child1(), m_node->child2());
                break;
            }
                
            case SetFunctionName: {
                considerBarrier(m_node->child1(), m_node->child2());
                break;
            }
                
            case NukeStructureAndSetButterfly: {
                considerBarrier(m_node->child1());
                break;
            }

            default:
                break;
            }
            
            if (doesGC(m_graph, m_node)) {
                m_currentEpoch.bump();
                potentialStackEscapes.clear();
            }
            
            switch (m_node->op()) {
            case NewObject:
            case NewGenerator:
            case NewAsyncGenerator:
            case NewArray:
            case NewArrayWithSize:
            case NewArrayWithConstantSize:
            case NewArrayWithSizeAndStructure:
            case NewArrayBuffer:
            case NewInternalFieldObject:
            case NewTypedArray:
            case NewTypedArrayBuffer:
            case NewRegexp:
            case NewStringObject:
            case NewMap:
            case NewSet:
            case NewSymbol:
            case MaterializeNewObject:
            case MaterializeNewArrayWithConstantSize:
            case MaterializeCreateActivation:
            case MakeRope:
            case MakeAtomString:
            case CreateActivation:
            case CreateDirectArguments:
            case CreateScopedArguments:
            case CreateClonedArguments:
            case NewFunction:
            case NewGeneratorFunction:
            case NewAsyncGeneratorFunction:
            case NewAsyncFunction:
            case NewBoundFunction:
            case AllocatePropertyStorage:
            case ReallocatePropertyStorage:
                // Nodes that allocate get to set their epoch because for those nodes we know
                // that they will be the newest object in the heap.
                m_node->setEpoch(m_currentEpoch);
                break;
                
            case Upsilon:
                // Assume the worst for Phis so that we don't have to worry about Phi shadows.
                m_node->phi()->setEpoch(Epoch());
                m_node->setEpoch(Epoch());
                break;
                
            default:
                // For nodes that aren't guaranteed to allocate, we say that their return value
                // (if there is one) could be arbitrarily old.
                m_node->setEpoch(Epoch());
                break;
            }

            {
                // We need to consider nodes that might leak objects we've allocated into the heap.
                // Once an object is leaked, we can no longer elide barriers on it.
                // Let's motivate this requirement with an example:
                // D@30: JSConstant(Int32: 42)
                // D@35: GetStack(arg1)
                // D@21: CheckStructure(Cell:D@35, [%ED:Object])
                // D@23: GetStack(arg2)
                // D@25: NewObject()
                // D@33: PutByOffset(KnownCell:D@25, KnownCell:D@25, Check:Untyped:Kill:D@30, id0{x})
                // D@34: PutStructure(KnownCell:D@25, %DN:Object -> %Ch:Object)
                // D@40: PutByOffset(KnownCell:D@35, KnownCell:D@35, Check:Untyped:D@25, id1{p})
                // D@45: FencedStoreBarrier(Check:KnownCell:Kill:D@35)
                // <-- P1
                // D@41: PutByOffset(KnownCell:D@25, KnownCell:D@25, Check:Untyped:Kill:D@23, id2{y})
                // <-- P2
                //
                // Let's say at the program point P1, the barrier @45 didn't fire because @35 is already grey.
                // Because @35 is grey, at P1, let's say the concurrent marker marks and traces @35, and also
                // marks and traces @25. So at P1, the concurrent marker blackens @35 and @25.
                // Now, let's consider program point P2.
                // If we didn't barrier @25 at P2, we will never see that @25 points to @23, because @25 is already
                // black. This is because after @25 was allocated, it escaped into the heap (at @40). Once an allocation
                // escapes into the heap, it can be blackened at any point by the concurrent marker.
                // So this analysis must mark an allocation that escapes to the heap as being part of the primordial
                // epoch.

                auto readFunc = [&] (const AbstractHeap& heap) {
                    if (!heap.overlaps(Stack))
                        return;
                    potentialStackEscapes.removeIf([&] (const auto& entry) {
                        if (entry.key.overlaps(heap)) {
                            entry.value->setEpoch(Epoch());
                            return true;
                        }
                        return false;
                    });
                };

                bool wroteHeapOrStack = false;
                unsigned numberOfPreciseStackWrites = 0;
                AbstractHeap preciseStackWrite;
                auto writeFunc = [&] (const AbstractHeap& heap) {
                    wroteHeapOrStack |= heap.overlaps(Heap) || heap.overlaps(Stack);
                    if (heap.kind() == Stack && !heap.payload().isTop()) {
                        ++numberOfPreciseStackWrites;
                        preciseStackWrite = heap;
                    }
                };
                clobberize(m_graph, m_node, readFunc, writeFunc, NoOpClobberize());

                if (wroteHeapOrStack) {
                    auto escape = [&] (Node* node) {
                        node->setEpoch(Epoch());
                    };

                    auto escapeToTheStack = [&] (Node* node) {
                        if (node->epoch() == m_currentEpoch) {
                            RELEASE_ASSERT(!!preciseStackWrite);
                            RELEASE_ASSERT(numberOfPreciseStackWrites == 1);
                            potentialStackEscapes.set(preciseStackWrite, node);
                        }
                    };

                    switch (m_node->op()) {
                    case PutStructure:
                    case MultiDeleteByOffset:
                        break;
                    case PutInternalField:
                        escape(m_node->child2().node());
                        break;
                    case PutByOffset:
                        escape(m_node->child3().node());
                        break;
                    case MultiPutByOffset:
                        escape(m_node->child2().node());
                        break;
                    case PutClosureVar:
                        escape(m_node->child2().node());
                        break;
                    case NukeStructureAndSetButterfly:
                        escape(m_node->child2().node());
                        break;
                    case SetLocal:
                    case PutStack:
                        escapeToTheStack(m_node->child1().node());
                        break;
                    default:
                        m_graph.doToChildren(m_node, [&] (Edge edge) {
                            escape(edge.node());
                        });
                        break;
                    }
                }
            }
            
            if (DFGStoreBarrierInsertionPhaseInternal::verbose) {
                WTF::dataFile().atomically([&](auto&) {
                    dataLog(
                        "    ", m_currentEpoch, ": Done with node ", m_node, " (", m_node->epoch(),
                        ") with children: ");
                    CommaPrinter comma;
                    m_graph.doToChildren(
                        m_node,
                        [&] (Edge edge) {
                            dataLog(comma, edge, " (", edge->epoch(), ")");
                        });
                    dataLogLn();
                });
            }
            
            if (mode == PhaseMode::Global) {
                if (!m_interpreter->executeEffects(m_nodeIndex, m_node)) {
                    result = false;
                    break;
                }
            }
        }

        {
            for (auto* node : potentialStackEscapes.values())
                node->setEpoch(Epoch());
            potentialStackEscapes.clear();
        }
        
        if (mode == PhaseMode::Global)
            m_state->reset();

        if (reallyInsertBarriers())
            m_insertionSet.execute(block);
        
        return result;
    }
    
    void considerBarrier(Edge base, Edge child)
    {
        dataLogLnIf(DFGStoreBarrierInsertionPhaseInternal::verbose, "        Considering adding barrier ", base, " => ", child);
        
        // We don't need a store barrier if the child is guaranteed to not be a cell.
        switch (mode) {
        case PhaseMode::Fast: {
            // Don't try too hard because it's too expensive to run AI.
            if (child->hasConstant()) {
                if (!child->asJSValue().isCell()) {
                    dataLogLnIf(DFGStoreBarrierInsertionPhaseInternal::verbose, "            Rejecting because of constant type.");
                    return;
                }
            } else {
                switch (child->result()) {
                case NodeResultNumber:
                case NodeResultDouble:
                case NodeResultInt32:
                case NodeResultInt52:
                case NodeResultBoolean:
                    dataLogLnIf(DFGStoreBarrierInsertionPhaseInternal::verbose, "            Rejecting because of result type.");
                    return;
                default:
                    break;
                }
            }
            break;
        }
            
        case PhaseMode::Global: {
            // Go into rage mode to eliminate any chance of a barrier with a non-cell child. We
            // can afford to keep around AI in Global mode.
            if (!m_interpreter->needsTypeCheck(child, ~SpecCell)) {
                dataLogLnIf(DFGStoreBarrierInsertionPhaseInternal::verbose, "            Rejecting because of AI type.");
                return;
            }
            break;
        } }
        
        considerBarrier(base);
    }
    
    void considerBarrier(Edge base)
    {
        dataLogLnIf(DFGStoreBarrierInsertionPhaseInternal::verbose, "        Considering adding barrier on ", base);
        
        // We don't need a store barrier if the epoch of the base is identical to the current
        // epoch. That means that we either just allocated the object and so it's guaranteed to
        // be in newgen, or we just ran a barrier on it so it's guaranteed to be remembered
        // already.
        if (base->epoch() == m_currentEpoch) {
            dataLogLnIf(DFGStoreBarrierInsertionPhaseInternal::verbose, "            Rejecting because it's in the current epoch.");
            return;
        }
        
        dataLogLnIf(DFGStoreBarrierInsertionPhaseInternal::verbose, "            Inserting barrier.");
        insertBarrier(m_nodeIndex + 1, base);
    }

    void insertBarrier(unsigned nodeIndex, Edge base)
    {
        // This is just our way of saying that barriers are not redundant with each other according
        // to forward analysis: if we proved one time that a barrier was necessary then it'll for
        // sure be necessary next time.
        base->setEpoch(Epoch());

        // If we're in global mode, we should only insert the barriers once we have converged.
        if (!reallyInsertBarriers())
            return;
        
        // FIXME: We could support StoreBarrier(UntypedUse:). That would be sort of cool.
        // But right now we don't need it.
        // https://bugs.webkit.org/show_bug.cgi?id=209396

        DFG_ASSERT(m_graph, m_node, isCell(base.useKind()), m_node->op(), base.useKind());
        
        // Barriers are always inserted after the node that they service. Therefore, we always know
        // that the thing is a cell now.
        base.setUseKind(KnownCellUse);
        
        NodeOrigin origin = m_node->origin;
        if (clobbersExitState(m_graph, m_node))
            origin = origin.withInvalidExit();
        
        m_insertionSet.insertNode(nodeIndex, SpecNone, FencedStoreBarrier, origin, base);
    }
    
    bool reallyInsertBarriers()
    {
        return mode == PhaseMode::Fast || m_isConverged;
    }
    
    InsertionSet m_insertionSet;
    Epoch m_currentEpoch;
    unsigned m_nodeIndex;
    Node* m_node;
    
    // Things we only use in Global mode.
    std::unique_ptr<InPlaceAbstractState> m_state;
    std::unique_ptr<AbstractInterpreter<InPlaceAbstractState>> m_interpreter;
    std::unique_ptr<BlockMap<UncheckedKeyHashSet<Node*>>> m_stateAtHead;
    std::unique_ptr<BlockMap<UncheckedKeyHashSet<Node*>>> m_stateAtTail;
    bool m_isConverged;
};

} // anonymous namespace

bool performFastStoreBarrierInsertion(Graph& graph)
{
    return runPhase<StoreBarrierInsertionPhase<PhaseMode::Fast>>(graph);
}

bool performGlobalStoreBarrierInsertion(Graph& graph)
{
    return runPhase<StoreBarrierInsertionPhase<PhaseMode::Global>>(graph);
}

} } // namespace JSC::DFG

#endif // ENABLE(DFG_JIT)

