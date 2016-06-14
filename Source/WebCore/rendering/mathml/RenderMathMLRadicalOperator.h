/*
 * Copyright (C) 2014 Frédéric Wang (fred.wang@free.fr). All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef RenderMathMLOperator_h
#define RenderMathMLRadicalOperator_h

#if ENABLE(MATHML)

#include "PaintInfo.h"
#include "RenderMathMLOperator.h"

namespace WebCore {

class RenderMathMLRadicalOperator final : public RenderMathMLOperator {
public:
    RenderMathMLRadicalOperator(Document&, RenderStyle&&);
    void stretchTo(LayoutUnit heightAboveBaseline, LayoutUnit depthBelowBaseline) override;
    void computePreferredLogicalWidths() override;
    void computeLogicalHeight(LayoutUnit logicalHeight, LayoutUnit logicalTop, LogicalExtentComputedValues&) const override;
    void paint(PaintInfo&, const LayoutPoint& paintOffset) override;

private:
    bool isRenderMathMLRadicalOperator() const override { return true; }
    const char* renderName() const override { return isAnonymous() ? "RenderMathMLRadicalOperator (anonymous)" : "RenderMathMLRadicalOperator"; }
    void setOperatorProperties() override;
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_RENDER_OBJECT(RenderMathMLRadicalOperator, isRenderMathMLRadicalOperator())

#endif // ENABLE(MATHML)
#endif // RenderMathMLRadicalOperator_h
