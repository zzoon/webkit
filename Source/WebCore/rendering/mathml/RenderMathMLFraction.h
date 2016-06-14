/*
 * Copyright (C) 2009 Alex Milowski (alex@milowski.com). All rights reserved.
 * Copyright (C) 2010 François Sausset (sausset@gmail.com). All rights reserved.
 * Copyright (C) 2016 Igalia S.L.
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

#ifndef RenderMathMLFraction_h
#define RenderMathMLFraction_h

#if ENABLE(MATHML)

#include "MathMLInlineContainerElement.h"
#include "RenderMathMLBlock.h"

namespace WebCore {

class RenderMathMLFraction final : public RenderMathMLBlock {
public:
    RenderMathMLFraction(MathMLInlineContainerElement&, RenderStyle&&);

    MathMLInlineContainerElement& element() { return static_cast<MathMLInlineContainerElement&>(nodeForNonAnonymous()); }
    float relativeLineThickness() const { return m_defaultLineThickness ? m_lineThickness / m_defaultLineThickness : LayoutUnit(0); }

    void layoutBlock(bool relayoutChildren, LayoutUnit pageLogicalHeight = 0) final;
    void paintChildren(PaintInfo& forSelf, const LayoutPoint&, PaintInfo& forChild, bool usePrintRect) final;

protected:
    void computePreferredLogicalWidths() final;

private:
    bool isRenderMathMLFraction() const final { return true; }
    const char* renderName() const final { return "RenderMathMLFraction"; }

    void updateFromElement() final;
    Optional<int> firstLineBaseline() const final;
    void paint(PaintInfo&, const LayoutPoint&) final;
    RenderMathMLOperator* unembellishedOperator() final;
    void styleDidChange(StyleDifference, const RenderStyle* oldStyle) final;
    
    bool isValid() const;
    RenderBox& numerator() const;
    RenderBox& denominator() const;
    enum FractionAlignment {
        FractionAlignmentCenter,
        FractionAlignmentLeft,
        FractionAlignmentRight
    };
    FractionAlignment parseAlignmentAttribute(const String& value);
    LayoutUnit horizontalOffset(RenderBox&, FractionAlignment);

    LayoutUnit m_defaultLineThickness;
    LayoutUnit m_lineThickness;
    FractionAlignment m_numeratorAlign;
    FractionAlignment m_denominatorAlign;
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_RENDER_OBJECT(RenderMathMLFraction, isRenderMathMLFraction())

#endif // ENABLE(MATHML)

#endif // RenderMathMLFraction_h
