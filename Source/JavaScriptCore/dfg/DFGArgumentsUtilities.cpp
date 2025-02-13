/*
 * Copyright (C) 2015 Apple Inc. All rights reserved.
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

#include "config.h"
#include "DFGArgumentsUtilities.h"

#if ENABLE(DFG_JIT)

#include "JSCInlines.h"

namespace JSC { namespace DFG {

bool argumentsInvolveStackSlot(InlineCallFrame* inlineCallFrame, VirtualRegister reg)
{
    if (!inlineCallFrame)
        return (reg.isArgument() && reg.toArgument()) || reg.isHeader();
    
    if (inlineCallFrame->isClosureCall
        && reg == VirtualRegister(inlineCallFrame->stackOffset + JSStack::Callee))
        return true;
    
    if (inlineCallFrame->isVarargs()
        && reg == VirtualRegister(inlineCallFrame->stackOffset + JSStack::ArgumentCount))
        return true;
    
    unsigned numArguments = inlineCallFrame->arguments.size() - 1;
    VirtualRegister argumentStart =
        VirtualRegister(inlineCallFrame->stackOffset) + CallFrame::argumentOffset(0);
    return reg >= argumentStart && reg < argumentStart + numArguments;
}

bool argumentsInvolveStackSlot(Node* candidate, VirtualRegister reg)
{
    return argumentsInvolveStackSlot(candidate->origin.semantic.inlineCallFrame, reg);
}

Node* emitCodeToGetArgumentsArrayLength(
    InsertionSet& insertionSet, Node* arguments, unsigned nodeIndex, NodeOrigin origin)
{
    Graph& graph = insertionSet.graph();

    DFG_ASSERT(
        graph, arguments,
        arguments->op() == CreateDirectArguments || arguments->op() == CreateScopedArguments
        || arguments->op() == CreateClonedArguments || arguments->op() == PhantomDirectArguments
        || arguments->op() == PhantomClonedArguments);
    
    InlineCallFrame* inlineCallFrame = arguments->origin.semantic.inlineCallFrame;
    
    if (inlineCallFrame && !inlineCallFrame->isVarargs()) {
        return insertionSet.insertConstant(
            nodeIndex, origin, jsNumber(inlineCallFrame->arguments.size() - 1));
    }
    
    Node* argumentCount;
    if (!inlineCallFrame)
        argumentCount = insertionSet.insertNode(nodeIndex, SpecInt32Only, GetArgumentCountIncludingThis, origin);
    else {
        VirtualRegister argumentCountRegister(inlineCallFrame->stackOffset + JSStack::ArgumentCount);
        
        argumentCount = insertionSet.insertNode(
            nodeIndex, SpecInt32Only, GetStack, origin,
            OpInfo(graph.m_stackAccessData.add(argumentCountRegister, FlushedInt32)));
    }
    
    return insertionSet.insertNode(
        nodeIndex, SpecInt32Only, ArithSub, origin, OpInfo(Arith::Unchecked),
        Edge(argumentCount, Int32Use),
        insertionSet.insertConstantForUse(
            nodeIndex, origin, jsNumber(1), Int32Use));
}

} } // namespace JSC::DFG

#endif // ENABLE(DFG_JIT)

