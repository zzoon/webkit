/*
 * Copyright (C) 2010 Alex Milowski (alex@milowski.com). All rights reserved.
 * Copyright (C) 2010 François Sausset (sausset@gmail.com). All rights reserved.
 * Copyright (C) 2013 Igalia S.L.
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

#include "config.h"

#if ENABLE(MATHML)

#include "RenderMathMLOperator.h"

#include "FontSelector.h"
#include "MathMLNames.h"
#include "PaintInfo.h"
#include "RenderBlockFlow.h"
#include "RenderText.h"
#include "ScaleTransformOperation.h"
#include "TransformOperations.h"
#include <wtf/MathExtras.h>
#include <wtf/unicode/CharacterNames.h>

namespace WebCore {
    
using namespace MathMLNames;

// FIXME: The OpenType MATH table contains information that should override this table (http://wkbug/122297).
struct StretchyCharacter {
    UChar character;
    UChar topChar;
    UChar extensionChar;
    UChar bottomChar;
    UChar middleChar;
};
// The first leftRightPairsCount pairs correspond to left/right fences that can easily be mirrored in RTL.
static const short leftRightPairsCount = 5;
static const StretchyCharacter stretchyCharacters[14] = {
    { 0x28  , 0x239b, 0x239c, 0x239d, 0x0    }, // left parenthesis
    { 0x29  , 0x239e, 0x239f, 0x23a0, 0x0    }, // right parenthesis
    { 0x5b  , 0x23a1, 0x23a2, 0x23a3, 0x0    }, // left square bracket
    { 0x5d  , 0x23a4, 0x23a5, 0x23a6, 0x0    }, // right square bracket
    { 0x7b  , 0x23a7, 0x23aa, 0x23a9, 0x23a8 }, // left curly bracket
    { 0x7d  , 0x23ab, 0x23aa, 0x23ad, 0x23ac }, // right curly bracket
    { 0x2308, 0x23a1, 0x23a2, 0x23a2, 0x0    }, // left ceiling
    { 0x2309, 0x23a4, 0x23a5, 0x23a5, 0x0    }, // right ceiling
    { 0x230a, 0x23a2, 0x23a2, 0x23a3, 0x0    }, // left floor
    { 0x230b, 0x23a5, 0x23a5, 0x23a6, 0x0    }, // right floor
    { 0x7c  , 0x7c,   0x7c,   0x7c,   0x0    }, // vertical bar
    { 0x2016, 0x2016, 0x2016, 0x2016, 0x0    }, // double vertical line
    { 0x2225, 0x2225, 0x2225, 0x2225, 0x0    }, // parallel to
    { 0x222b, 0x2320, 0x23ae, 0x2321, 0x0    } // integral sign
};

RenderMathMLOperator::RenderMathMLOperator(MathMLElement& element, RenderStyle&& style)
    : RenderMathMLToken(element, WTFMove(style))
    , m_stretchHeightAboveBaseline(0)
    , m_stretchDepthBelowBaseline(0)
    , m_textContent(0)
    , m_isVertical(true)
{
    updateTokenContent();
}

RenderMathMLOperator::RenderMathMLOperator(Document& document, RenderStyle&& style, const String& operatorString, MathMLOperatorDictionary::Form form, unsigned short flags)
    : RenderMathMLToken(document, WTFMove(style))
    , m_stretchHeightAboveBaseline(0)
    , m_stretchDepthBelowBaseline(0)
    , m_textContent(0)
    , m_isVertical(true)
    , m_operatorForm(form)
    , m_operatorFlags(flags)
{
    updateTokenContent(operatorString);
}

void RenderMathMLOperator::setOperatorFlagFromAttribute(MathMLOperatorDictionary::Flag flag, const QualifiedName& name)
{
    setOperatorFlagFromAttributeValue(flag, element().fastGetAttribute(name));
}

void RenderMathMLOperator::setOperatorFlagFromAttributeValue(MathMLOperatorDictionary::Flag flag, const AtomicString& attributeValue)
{
    ASSERT(!isAnonymous());

    if (attributeValue == "true")
        m_operatorFlags |= flag;
    else if (attributeValue == "false")
        m_operatorFlags &= ~flag;
    // We ignore absent or invalid attributes.
}

void RenderMathMLOperator::setOperatorPropertiesFromOpDictEntry(const MathMLOperatorDictionary::Entry* entry)
{
    // If this operator is anonymous, we preserve the Fence and Separator properties. This is to handle the case of RenderMathMLFenced.
    if (isAnonymous())
        m_operatorFlags = (m_operatorFlags & (MathMLOperatorDictionary::Fence | MathMLOperatorDictionary::Separator)) | entry->flags;
    else
        m_operatorFlags = entry->flags;

    // Leading and trailing space is specified as multiple of 1/18em.
    m_leadingSpace = entry->lspace * style().fontCascade().size() / 18;
    m_trailingSpace = entry->rspace * style().fontCascade().size() / 18;
}

void RenderMathMLOperator::setOperatorProperties()
{
    // We determine the stretch direction (default is vertical).
    m_isVertical = MathMLOperatorDictionary::isVertical(m_textContent);

    // We determine the form of the operator.
    bool explicitForm = true;
    if (!isAnonymous()) {
        const AtomicString& form = element().fastGetAttribute(MathMLNames::formAttr);
        if (form == "prefix")
            m_operatorForm = MathMLOperatorDictionary::Prefix;
        else if (form == "infix")
            m_operatorForm = MathMLOperatorDictionary::Infix;
        else if (form == "postfix")
            m_operatorForm = MathMLOperatorDictionary::Postfix;
        else {
            // FIXME: We should use more advanced heuristics indicated in the specification to determine the operator form (https://bugs.webkit.org/show_bug.cgi?id=124829).
            explicitForm = false;
            if (!element().previousSibling() && element().nextSibling())
                m_operatorForm = MathMLOperatorDictionary::Prefix;
            else if (element().previousSibling() && !element().nextSibling())
                m_operatorForm = MathMLOperatorDictionary::Postfix;
            else
                m_operatorForm = MathMLOperatorDictionary::Infix;
        }
    }

    // We determine the default values of the operator properties.

    // First we initialize with the default values for unknown operators.
    if (isAnonymous())
        m_operatorFlags &= MathMLOperatorDictionary::Fence | MathMLOperatorDictionary::Separator; // This resets all but the Fence and Separator properties.
    else
        m_operatorFlags = 0; // This resets all the operator properties.
    m_leadingSpace = 5 * style().fontCascade().size() / 18; // This sets leading space to "thickmathspace".
    m_trailingSpace = 5 * style().fontCascade().size() / 18; // This sets trailing space to "thickmathspace".
    m_minSize = style().fontCascade().size(); // This sets minsize to "1em".
    m_maxSize = intMaxForLayoutUnit; // This sets maxsize to "infinity".

    if (m_textContent) {
        // Then we try to find the default values from the operator dictionary.
        if (const MathMLOperatorDictionary::Entry* entry = MathMLOperatorDictionary::getEntry(m_textContent, m_operatorForm))
            setOperatorPropertiesFromOpDictEntry(entry);
        else if (!explicitForm) {
            // If we did not find the desired operator form and if it was not set explicitely, we use the first one in the following order: Infix, Prefix, Postfix.
            // This is to handle bad MathML markup without explicit <mrow> delimiters like "<mo>(</mo><mi>a</mi><mo>)</mo><mo>(</mo><mi>b</mi><mo>)</mo>" where the inner parenthesis should not be considered infix.
            if (const MathMLOperatorDictionary::Entry* entry = MathMLOperatorDictionary::getEntry(m_textContent)) {
                m_operatorForm = static_cast<MathMLOperatorDictionary::Form>(entry->form); // We override the form previously determined.
                setOperatorPropertiesFromOpDictEntry(entry);
            }
        }
    }

    if (!isAnonymous()) {
        // Finally, we make the attribute values override the default.

        setOperatorFlagFromAttribute(MathMLOperatorDictionary::Fence, MathMLNames::fenceAttr);
        setOperatorFlagFromAttribute(MathMLOperatorDictionary::Separator, MathMLNames::separatorAttr);
        setOperatorFlagFromAttribute(MathMLOperatorDictionary::Stretchy, MathMLNames::stretchyAttr);
        setOperatorFlagFromAttribute(MathMLOperatorDictionary::Symmetric, MathMLNames::symmetricAttr);
        setOperatorFlagFromAttribute(MathMLOperatorDictionary::LargeOp, MathMLNames::largeopAttr);
        setOperatorFlagFromAttribute(MathMLOperatorDictionary::MovableLimits, MathMLNames::movablelimitsAttr);
        setOperatorFlagFromAttribute(MathMLOperatorDictionary::Accent, MathMLNames::accentAttr);

        parseMathMLLength(element().fastGetAttribute(MathMLNames::lspaceAttr), m_leadingSpace, &style(), false); // FIXME: Negative leading space must be implemented (https://bugs.webkit.org/show_bug.cgi?id=124830).
        parseMathMLLength(element().fastGetAttribute(MathMLNames::rspaceAttr), m_trailingSpace, &style(), false); // FIXME: Negative trailing space must be implemented (https://bugs.webkit.org/show_bug.cgi?id=124830).

        parseMathMLLength(element().fastGetAttribute(MathMLNames::minsizeAttr), m_minSize, &style(), false);
        const AtomicString& maxsize = element().fastGetAttribute(MathMLNames::maxsizeAttr);
        if (maxsize != "infinity")
            parseMathMLLength(maxsize, m_maxSize, &style(), false);
    }
}

bool RenderMathMLOperator::isChildAllowed(const RenderObject&, const RenderStyle&) const
{
    return false;
}

void RenderMathMLOperator::stretchTo(LayoutUnit heightAboveBaseline, LayoutUnit depthBelowBaseline)
{
    ASSERT(hasOperatorFlag(MathMLOperatorDictionary::Stretchy));
    ASSERT(m_isVertical);

    if (!m_isVertical || (heightAboveBaseline == m_stretchHeightAboveBaseline && depthBelowBaseline == m_stretchDepthBelowBaseline))
        return;

    m_stretchHeightAboveBaseline = heightAboveBaseline;
    m_stretchDepthBelowBaseline = depthBelowBaseline;

    setOperatorProperties();
    if (hasOperatorFlag(MathMLOperatorDictionary::Symmetric)) {
        // We make the operator stretch symmetrically above and below the axis.
        // FIXME: We should read the axis from the MATH table (https://bugs.webkit.org/show_bug.cgi?id=122297). For now, we use the same value as in RenderMathMLFraction::firstLineBaseline().
        LayoutUnit axis = style().fontMetrics().xHeight() / 2;
        LayoutUnit halfStretchSize = std::max(m_stretchHeightAboveBaseline - axis, m_stretchDepthBelowBaseline + axis);
        m_stretchHeightAboveBaseline = halfStretchSize + axis;
        m_stretchDepthBelowBaseline = halfStretchSize - axis;
    }
    // We try to honor the minsize/maxsize condition by increasing or decreasing both height and depth proportionately.
    // The MathML specification does not indicate what to do when maxsize < minsize, so we follow Gecko and make minsize take precedence.
    LayoutUnit size = stretchSize();
    float aspect = 1.0;
    if (size > 0) {
        if (size < m_minSize)
            aspect = float(m_minSize) / size;
        else if (m_maxSize < size)
            aspect = float(m_maxSize) / size;
    }
    m_stretchHeightAboveBaseline *= aspect;
    m_stretchDepthBelowBaseline *= aspect;
    updateStyle();
}

void RenderMathMLOperator::stretchTo(LayoutUnit width)
{
    ASSERT(hasOperatorFlag(MathMLOperatorDictionary::Stretchy));
    ASSERT(!m_isVertical);

    if (m_isVertical || m_stretchWidth == width)
        return;

    m_stretchWidth = width;

    setOperatorProperties();

    updateStyle();
}

void RenderMathMLOperator::resetStretchSize()
{
    if (m_isVertical) {
        m_stretchHeightAboveBaseline = 0;
        m_stretchDepthBelowBaseline = 0;
    } else
        m_stretchWidth = 0;
}

static inline FloatRect boundsForGlyph(const GlyphData& data)
{
    return data.font && data.glyph ? data.font->boundsForGlyph(data.glyph) : FloatRect();
}

static inline float heightForGlyph(const GlyphData& data)
{
    return boundsForGlyph(data).height();
}

static inline float advanceWidthForGlyph(const GlyphData& data)
{
    return data.font && data.glyph ? data.font->widthForGlyph(data.glyph) : 0;
}

void RenderMathMLOperator::computePreferredLogicalWidths()
{
    ASSERT(preferredLogicalWidthsDirty());

    setOperatorProperties();
    if (!shouldAllowStretching()) {
        RenderMathMLToken::computePreferredLogicalWidths();
        if (isInvisibleOperator()) {
            // In some fonts, glyphs for invisible operators have nonzero width. Consequently, we subtract that width here to avoid wide gaps.
            GlyphData data = style().fontCascade().glyphDataForCharacter(m_textContent, false);
            float glyphWidth = advanceWidthForGlyph(data);
            ASSERT(glyphWidth <= m_minPreferredLogicalWidth);
            m_minPreferredLogicalWidth -= glyphWidth;
            m_maxPreferredLogicalWidth -= glyphWidth;
        }
        return;
    }

    GlyphData baseGlyph;
    float maximumGlyphWidth = getBaseGlyph(style(), baseGlyph) ? advanceWidthForGlyph(baseGlyph) : 0;
    if (!m_isVertical) {
        if (maximumGlyphWidth < stretchSize())
            maximumGlyphWidth = stretchSize();
        m_maxPreferredLogicalWidth = m_leadingSpace + maximumGlyphWidth + m_trailingSpace;
        m_minPreferredLogicalWidth = m_maxPreferredLogicalWidth;
        return;
    }
    if (isLargeOperatorInDisplayStyle()) {
        // Large operators in STIX Word have incorrect advance width, causing misplacement of superscript, so we use the glyph bound instead (http://sourceforge.net/p/stixfonts/tracking/49/).
        calculateDisplayStyleLargeOperator();
        if (m_stretchType == StretchType::SizeVariant)
            maximumGlyphWidth = boundsForGlyph(m_variant).width();
    } else {
        // FIXME: some glyphs (e.g. the one for "FRACTION SLASH" in the STIX Math font or large operators) have a width that depends on the height, resulting in large gaps (https://bugs.webkit.org/show_bug.cgi?id=130326).
        calculateStretchyData(&maximumGlyphWidth);
    }
    m_maxPreferredLogicalWidth = m_minPreferredLogicalWidth = m_leadingSpace + maximumGlyphWidth + m_trailingSpace;
}

void RenderMathMLOperator::rebuildTokenContent(const String& operatorString)
{
    // We collapse the whitespace and replace the hyphens by minus signs.
    AtomicString textContent = operatorString.stripWhiteSpace().simplifyWhiteSpace().replace(hyphenMinus, minusSign).impl();

    // We destroy the wrapper and rebuild it.
    // FIXME: Using this RenderText make the text inaccessible to the dumpAsText/selection code (https://bugs.webkit.org/show_bug.cgi?id=125597).
    if (firstChild())
        downcast<RenderElement>(*firstChild()).destroy();
    createWrapperIfNeeded();
    RenderPtr<RenderText> text = createRenderer<RenderText>(document(), textContent);
    downcast<RenderElement>(*firstChild()).addChild(text.leakPtr());

    // We verify whether the operator text can be represented by a single UChar.
    // FIXME: This does not handle surrogate pairs (https://bugs.webkit.org/show_bug.cgi?id=122296).
    // FIXME: This does not handle <mo> operators with multiple characters (https://bugs.webkit.org/show_bug.cgi?id=124828).
    m_textContent = textContent.length() == 1 ? textContent[0] : 0;
    setOperatorProperties();
    updateStyle();
    setNeedsLayoutAndPrefWidthsRecalc();
}

void RenderMathMLOperator::updateTokenContent(const String& operatorString)
{
    ASSERT(isAnonymous());
    rebuildTokenContent(operatorString);
}

void RenderMathMLOperator::updateTokenContent()
{
    ASSERT(!isAnonymous());
    rebuildTokenContent(element().textContent());
}

void RenderMathMLOperator::updateFromElement()
{
    setOperatorProperties();
    RenderMathMLToken::updateFromElement();
}

void RenderMathMLOperator::updateOperatorProperties()
{
    setOperatorProperties();
    if (!isEmpty())
        updateStyle();
    setNeedsLayoutAndPrefWidthsRecalc();
}

bool RenderMathMLOperator::shouldAllowStretching() const
{
    return m_textContent && (hasOperatorFlag(MathMLOperatorDictionary::Stretchy) || isLargeOperatorInDisplayStyle());
}

void RenderMathMLOperator::styleDidChange(StyleDifference diff, const RenderStyle* oldStyle)
{
    RenderMathMLBlock::styleDidChange(diff, oldStyle);
    updateOperatorProperties();
}

bool RenderMathMLOperator::getBaseGlyph(const RenderStyle& style, GlyphData& baseGlyph) const
{
    baseGlyph = style.fontCascade().glyphDataForCharacter(m_textContent, !style.isLeftToRightDirection());
    return baseGlyph.isValid() && baseGlyph.font == &style.fontCascade().primaryFont();
}

void RenderMathMLOperator::setSizeVariant(const GlyphData& sizeVariant)
{
    ASSERT(sizeVariant.isValid() && sizeVariant.font->mathData());
    m_stretchType = StretchType::SizeVariant;
    m_variant = sizeVariant;
}

void RenderMathMLOperator::setGlyphAssembly(const GlyphAssemblyData& assemblyData)
{
    m_stretchType = StretchType::GlyphAssembly;
    m_assembly = assemblyData;
}

bool RenderMathMLOperator::calculateGlyphAssemblyFallBack(const Vector<OpenTypeMathData::AssemblyPart>& assemblyParts, GlyphAssemblyData& assemblyData) const
{
    // The structure of the Open Type Math table is a bit more general than the one currently used by the RenderMathMLOperator code, so we try to fallback in a reasonable way.
    // FIXME: RenderMathMLOperator should support the most general format (https://bugs.webkit.org/show_bug.cgi?id=130327).
    // We use the approach of the copyComponents function in github.com/mathjax/MathJax-dev/blob/master/fonts/OpenTypeMath/fontUtil.py

    // We count the number of non extender pieces.
    int nonExtenderCount = 0;
    for (auto& part : assemblyParts) {
        if (!part.isExtender)
            nonExtenderCount++;
    }
    if (nonExtenderCount > 3)
        return false; // This is not supported: there are too many pieces.

    // We now browse the list of pieces from left to right for horizontal operators and from bottom to top for vertical operators.
    enum PartType {
        Start,
        ExtenderBetweenStartAndMiddle,
        Middle,
        ExtenderBetweenMiddleAndEnd,
        End,
        None
    };
    PartType expectedPartType = Start;
    assemblyData.extension.glyph = 0;
    assemblyData.middle.glyph = 0;
    for (auto& part : assemblyParts) {
        if (nonExtenderCount < 3) {
            // If we only have at most two non-extenders then we skip the middle glyph.
            if (expectedPartType == ExtenderBetweenStartAndMiddle)
                expectedPartType = ExtenderBetweenMiddleAndEnd;
            else if (expectedPartType == Middle)
                expectedPartType = End;
        }
        if (part.isExtender) {
            if (!assemblyData.extension.glyph)
                assemblyData.extension.glyph = part.glyph; // We copy the extender part.
            else if (assemblyData.extension.glyph != part.glyph)
                return false; // This is not supported: the assembly has different extenders.

            switch (expectedPartType) {
            case Start:
                // We ignore the left/bottom part.
                expectedPartType = ExtenderBetweenStartAndMiddle;
                continue;
            case Middle:
                // We ignore the middle part.
                expectedPartType = ExtenderBetweenMiddleAndEnd;
                continue;
            case End:
            case None:
                // This is not supported: we got an unexpected extender.
                return false;
            case ExtenderBetweenStartAndMiddle:
            case ExtenderBetweenMiddleAndEnd:
                // We ignore multiple consecutive extenders.
                continue;
            }
        }

        switch (expectedPartType) {
        case Start:
            // We copy the left/bottom part.
            assemblyData.bottomOrLeft.glyph = part.glyph;
            expectedPartType = ExtenderBetweenStartAndMiddle;
            continue;
        case ExtenderBetweenStartAndMiddle:
        case Middle:
            // We copy the middle part.
            assemblyData.middle.glyph = part.glyph;
            expectedPartType = ExtenderBetweenMiddleAndEnd;
            continue;
        case ExtenderBetweenMiddleAndEnd:
        case End:
            // We copy the right/top part.
            assemblyData.topOrRight.glyph = part.glyph;
            expectedPartType = None;
            continue;
        case None:
            // This is not supported: we got an unexpected non-extender part.
            return false;
        }
    }

    if (!assemblyData.extension.glyph)
        return false; // This is not supported: we always assume that we have an extension glyph.

    // If we don't have top/right or left/bottom glyphs, we use the extension glyph.
    if (!assemblyData.topOrRight.glyph)
        assemblyData.topOrRight.glyph = assemblyData.extension.glyph;
    if (!assemblyData.bottomOrLeft.glyph)
        assemblyData.bottomOrLeft.glyph = assemblyData.extension.glyph;

    assemblyData.topOrRight.font = &style().fontCascade().primaryFont();
    assemblyData.extension.font = assemblyData.topOrRight.font;
    assemblyData.bottomOrLeft.font = assemblyData.topOrRight.font;
    assemblyData.middle.font = assemblyData.middle.glyph ? assemblyData.topOrRight.font : nullptr;

    return true;
}

void RenderMathMLOperator::calculateDisplayStyleLargeOperator()
{
    ASSERT(m_isVertical && isLargeOperatorInDisplayStyle());

    GlyphData baseGlyph;
    if (!getBaseGlyph(style(), baseGlyph) || !baseGlyph.font->mathData())
        return;

    Vector<Glyph> sizeVariants;
    Vector<OpenTypeMathData::AssemblyPart> assemblyParts;

    // The value of displayOperatorMinHeight is sometimes too small, so we ensure that it is at least \sqrt{2} times the size of the base glyph.
    float displayOperatorMinHeight = std::max(baseGlyph.font->boundsForGlyph(baseGlyph.glyph).height() * sqrtOfTwoFloat, baseGlyph.font->mathData()->getMathConstant(*baseGlyph.font, OpenTypeMathData::DisplayOperatorMinHeight));

    baseGlyph.font->mathData()->getMathVariants(baseGlyph.glyph, true, sizeVariants, assemblyParts);

    // We choose the first size variant that is larger than the expected displayOperatorMinHeight and otherwise fallback to the largest variant.
    for (auto& sizeVariant : sizeVariants) {
        GlyphData glyphData(sizeVariant, baseGlyph.font);
        setSizeVariant(glyphData);
        m_italicCorrection = glyphData.font->mathData()->getItalicCorrection(*glyphData.font, glyphData.glyph);
        if (heightForGlyph(glyphData) >= displayOperatorMinHeight)
            break;
    }
}

void RenderMathMLOperator::calculateStretchyData(float* maximumGlyphWidth, LayoutUnit targetSize)
{
    ASSERT(!maximumGlyphWidth || m_isVertical);

    GlyphData baseGlyph;
    if (!getBaseGlyph(style(), baseGlyph))
        return;
    
    if (!maximumGlyphWidth) {
        // We do not stretch if the base glyph is large enough.
        float baseSize = m_isVertical ? heightForGlyph(baseGlyph) : advanceWidthForGlyph(baseGlyph);
        if (targetSize <= baseSize)
            return;
    }

    GlyphAssemblyData assemblyData;
    if (baseGlyph.font->mathData()) {
        Vector<Glyph> sizeVariants;
        Vector<OpenTypeMathData::AssemblyPart> assemblyParts;
        baseGlyph.font->mathData()->getMathVariants(baseGlyph.glyph, m_isVertical, sizeVariants, assemblyParts);
        // We verify the size variants.
        for (auto& sizeVariant : sizeVariants) {
            GlyphData glyphData(sizeVariant, baseGlyph.font);
            if (maximumGlyphWidth)
                *maximumGlyphWidth = std::max(*maximumGlyphWidth, advanceWidthForGlyph(glyphData));
            else {
                setSizeVariant(glyphData);
                float size = m_isVertical ? heightForGlyph(glyphData) : advanceWidthForGlyph(glyphData);
                if (size >= targetSize)
                    return;
            }
        }

        // We verify if there is a construction.
        if (!calculateGlyphAssemblyFallBack(assemblyParts, assemblyData))
            return;
    } else {
        if (!m_isVertical)
            return;

        // If the font does not have a MATH table, we fallback to the Unicode-only constructions.
        const StretchyCharacter* stretchyCharacter = nullptr;
        const unsigned maxIndex = WTF_ARRAY_LENGTH(stretchyCharacters);
        for (unsigned index = 0; index < maxIndex; ++index) {
            if (stretchyCharacters[index].character == m_textContent) {
                stretchyCharacter = &stretchyCharacters[index];
                if (!style().isLeftToRightDirection() && index < leftRightPairsCount * 2) {
                    // If we are in right-to-left direction we select the mirrored form by adding -1 or +1 according to the parity of index.
                    index += index % 2 ? -1 : 1;
                }
                break;
            }
        }

        // If we didn't find a stretchy character set for this character, we don't know how to stretch it.
        if (!stretchyCharacter)
            return;

        // We convert the list of Unicode characters into a list of glyph data.
        assemblyData.topOrRight = style().fontCascade().glyphDataForCharacter(stretchyCharacter->topChar, false);
        assemblyData.extension = style().fontCascade().glyphDataForCharacter(stretchyCharacter->extensionChar, false);
        assemblyData.bottomOrLeft = style().fontCascade().glyphDataForCharacter(stretchyCharacter->bottomChar, false);
        assemblyData.middle = stretchyCharacter->middleChar ? style().fontCascade().glyphDataForCharacter(stretchyCharacter->middleChar, false) : GlyphData();
    }

    // If we are measuring the maximum width, verify each component.
    if (maximumGlyphWidth) {
        *maximumGlyphWidth = std::max(*maximumGlyphWidth, advanceWidthForGlyph(assemblyData.topOrRight));
        *maximumGlyphWidth = std::max(*maximumGlyphWidth, advanceWidthForGlyph(assemblyData.extension));
        *maximumGlyphWidth = std::max(*maximumGlyphWidth, advanceWidthForGlyph(assemblyData.middle));
        *maximumGlyphWidth = std::max(*maximumGlyphWidth, advanceWidthForGlyph(assemblyData.bottomOrLeft));
        return;
    }

    // We ensure that the size is large enough to avoid glyph overlaps.
    float minSize = m_isVertical ?
        heightForGlyph(assemblyData.topOrRight) + heightForGlyph(assemblyData.middle) + heightForGlyph(assemblyData.bottomOrLeft)
        : advanceWidthForGlyph(assemblyData.bottomOrLeft) + advanceWidthForGlyph(assemblyData.middle) + advanceWidthForGlyph(assemblyData.topOrRight);
    if (minSize > targetSize)
        return;

    setGlyphAssembly(assemblyData);
}

void RenderMathMLOperator::updateStyle()
{
    ASSERT(firstChild());
    if (!firstChild())
        return;

    m_stretchType = StretchType::Unstretched;
    m_italicCorrection = 0;
    // We add spacing around the operator.
    // FIXME: The spacing should be added to the whole embellished operator (https://bugs.webkit.org/show_bug.cgi?id=124831).
    // FIXME: The spacing should only be added inside (perhaps inferred) mrow (http://www.w3.org/TR/MathML/chapter3.html#presm.opspacing).
    const auto& wrapper = downcast<RenderElement>(firstChild());
    auto newStyle = RenderStyle::createAnonymousStyleWithDisplay(style(), FLEX);
    newStyle.setMarginStart(Length(m_leadingSpace, Fixed));
    newStyle.setMarginEnd(Length(m_trailingSpace, Fixed));
    wrapper->setStyle(WTFMove(newStyle));
    wrapper->setNeedsLayoutAndPrefWidthsRecalc();

    if (!shouldAllowStretching())
        return;

    if (m_isVertical && isLargeOperatorInDisplayStyle())
        calculateDisplayStyleLargeOperator();
    else {
        calculateStretchyData(nullptr, stretchSize());
        if (m_stretchType == StretchType::Unstretched)
            return;
    }

    if (m_isVertical && m_stretchType == StretchType::SizeVariant) {
        // We resize the operator to match the one of the size variant.
        if (isLargeOperatorInDisplayStyle()) {
            // The stretch size is actually not involved in the selection of the size variant in findDisplayStyleLargeOperator.
            // We simply use the height and depth of the selected size variant glyph.
            FloatRect glyphBounds = boundsForGlyph(m_variant);
            m_stretchHeightAboveBaseline = -glyphBounds.y();
            m_stretchDepthBelowBaseline = glyphBounds.maxY();
        } else {
            // We rescale the height and depth proportionately.
            float variantSize = heightForGlyph(m_variant);
            float size = stretchSize();
            float aspect = size > 0 ? variantSize / size : 1.0;
            m_stretchHeightAboveBaseline *= aspect;
            m_stretchDepthBelowBaseline *= aspect;
        }
    }

    if (!m_isVertical) {
        if (m_stretchType == StretchType::SizeVariant) {
            FloatRect glyphBounds = boundsForGlyph(m_variant);
            m_stretchHeightAboveBaseline = -glyphBounds.y();
            m_stretchDepthBelowBaseline = glyphBounds.maxY();
            m_stretchWidth = advanceWidthForGlyph(m_variant);
        } else if (m_stretchType == StretchType::GlyphAssembly) {
            FloatRect glyphBounds;
            m_stretchHeightAboveBaseline = 0;
            m_stretchDepthBelowBaseline = 0;

            glyphBounds = boundsForGlyph(m_assembly.bottomOrLeft);
            m_stretchHeightAboveBaseline = std::max<LayoutUnit>(m_stretchHeightAboveBaseline, -glyphBounds.y());
            m_stretchDepthBelowBaseline = std::max<LayoutUnit>(m_stretchDepthBelowBaseline, glyphBounds.maxY());

            glyphBounds = boundsForGlyph(m_assembly.topOrRight);
            m_stretchHeightAboveBaseline = std::max<LayoutUnit>(m_stretchHeightAboveBaseline, -glyphBounds.y());
            m_stretchDepthBelowBaseline = std::max<LayoutUnit>(m_stretchDepthBelowBaseline, glyphBounds.maxY());

            glyphBounds = boundsForGlyph(m_assembly.extension);
            m_stretchHeightAboveBaseline = std::max<LayoutUnit>(m_stretchHeightAboveBaseline, -glyphBounds.y());
            m_stretchDepthBelowBaseline = std::max<LayoutUnit>(m_stretchDepthBelowBaseline, glyphBounds.maxY());

            if (m_assembly.middle.isValid()) {
                glyphBounds = boundsForGlyph(m_assembly.middle);
                m_stretchHeightAboveBaseline = std::max<LayoutUnit>(m_stretchHeightAboveBaseline, -glyphBounds.y());
                m_stretchDepthBelowBaseline = std::max<LayoutUnit>(m_stretchDepthBelowBaseline, glyphBounds.maxY());
            }
        }
    }
}

Optional<int> RenderMathMLOperator::firstLineBaseline() const
{
    if (m_stretchType != StretchType::Unstretched)
        return Optional<int>(m_stretchHeightAboveBaseline);
    return RenderMathMLToken::firstLineBaseline();
}

void RenderMathMLOperator::computeLogicalHeight(LayoutUnit logicalHeight, LayoutUnit logicalTop, LogicalExtentComputedValues& computedValues) const
{
    if (m_stretchType != StretchType::Unstretched)
        logicalHeight = m_stretchHeightAboveBaseline + m_stretchDepthBelowBaseline;
    RenderBox::computeLogicalHeight(logicalHeight, logicalTop, computedValues);
}

LayoutRect RenderMathMLOperator::paintGlyph(PaintInfo& info, const GlyphData& data, const LayoutPoint& origin, GlyphPaintTrimming trim)
{
    FloatRect glyphBounds = boundsForGlyph(data);

    LayoutRect glyphPaintRect(origin, LayoutSize(glyphBounds.x() + glyphBounds.width(), glyphBounds.height()));
    glyphPaintRect.setY(origin.y() + glyphBounds.y());

    // In order to have glyphs fit snugly with one another we snap the connecting edges to pixel boundaries
    // and trim off one pixel. The pixel trim is to account for fonts that have edge pixels that have less
    // than full coverage. These edge pixels can introduce small seams between connected glyphs.
    FloatRect clipBounds = info.rect;
    switch (trim) {
    case TrimTop:
        glyphPaintRect.shiftYEdgeTo(glyphPaintRect.y().ceil() + 1);
        clipBounds.shiftYEdgeTo(glyphPaintRect.y());
        break;
    case TrimBottom:
        glyphPaintRect.shiftMaxYEdgeTo(glyphPaintRect.maxY().floor() - 1);
        clipBounds.shiftMaxYEdgeTo(glyphPaintRect.maxY());
        break;
    case TrimTopAndBottom:
        glyphPaintRect.shiftYEdgeTo(glyphPaintRect.y().ceil() + 1);
        glyphPaintRect.shiftMaxYEdgeTo(glyphPaintRect.maxY().floor() - 1);
        clipBounds.shiftYEdgeTo(glyphPaintRect.y());
        clipBounds.shiftMaxYEdgeTo(glyphPaintRect.maxY());
        break;
    case TrimLeft:
        glyphPaintRect.shiftXEdgeTo(glyphPaintRect.x().ceil() + 1);
        clipBounds.shiftXEdgeTo(glyphPaintRect.x());
        break;
    case TrimRight:
        glyphPaintRect.shiftMaxXEdgeTo(glyphPaintRect.maxX().floor() - 1);
        clipBounds.shiftMaxXEdgeTo(glyphPaintRect.maxX());
        break;
    case TrimLeftAndRight:
        glyphPaintRect.shiftXEdgeTo(glyphPaintRect.x().ceil() + 1);
        glyphPaintRect.shiftMaxXEdgeTo(glyphPaintRect.maxX().floor() - 1);
        clipBounds.shiftXEdgeTo(glyphPaintRect.x());
        clipBounds.shiftMaxXEdgeTo(glyphPaintRect.maxX());
    }

    // Clipping the enclosing IntRect avoids any potential issues at joined edges.
    GraphicsContextStateSaver stateSaver(info.context());
    info.context().clip(clipBounds);

    GlyphBuffer buffer;
    buffer.add(data.glyph, data.font, advanceWidthForGlyph(data));
    info.context().drawGlyphs(style().fontCascade(), *data.font, buffer, 0, 1, origin);

    return glyphPaintRect;
}

void RenderMathMLOperator::fillWithVerticalExtensionGlyph(PaintInfo& info, const LayoutPoint& from, const LayoutPoint& to)
{
    ASSERT(m_isVertical);
    ASSERT(m_stretchType == StretchType::GlyphAssembly);
    ASSERT(m_assembly.extension.isValid());
    ASSERT(from.y() <= to.y());

    // If there is no space for the extension glyph, we don't need to do anything.
    if (from.y() == to.y())
        return;

    GraphicsContextStateSaver stateSaver(info.context());

    FloatRect glyphBounds = boundsForGlyph(m_assembly.extension);

    // Clipping the extender region here allows us to draw the bottom extender glyph into the
    // regions of the bottom glyph without worrying about overdraw (hairy pixels) and simplifies later clipping.
    LayoutRect clipBounds = info.rect;
    clipBounds.shiftYEdgeTo(from.y());
    clipBounds.shiftMaxYEdgeTo(to.y());
    info.context().clip(clipBounds);

    // Trimming may remove up to two pixels from the top of the extender glyph, so we move it up by two pixels.
    float offsetToGlyphTop = glyphBounds.y() + 2;
    LayoutPoint glyphOrigin = LayoutPoint(from.x(), from.y() - offsetToGlyphTop);
    FloatRect lastPaintedGlyphRect(from, FloatSize());

    while (lastPaintedGlyphRect.maxY() < to.y()) {
        lastPaintedGlyphRect = paintGlyph(info, m_assembly.extension, glyphOrigin, TrimTopAndBottom);
        glyphOrigin.setY(glyphOrigin.y() + lastPaintedGlyphRect.height());

        // There's a chance that if the font size is small enough the glue glyph has been reduced to an empty rectangle
        // with trimming. In that case we just draw nothing.
        if (lastPaintedGlyphRect.isEmpty())
            break;
    }
}

void RenderMathMLOperator::fillWithHorizontalExtensionGlyph(PaintInfo& info, const LayoutPoint& from, const LayoutPoint& to)
{
    ASSERT(!m_isVertical);
    ASSERT(m_stretchType == StretchType::GlyphAssembly);
    ASSERT(m_assembly.extension.isValid());
    ASSERT(from.x() <= to.x());

    // If there is no space for the extension glyph, we don't need to do anything.
    if (from.x() == to.x())
        return;

    GraphicsContextStateSaver stateSaver(info.context());

    // Clipping the extender region here allows us to draw the bottom extender glyph into the
    // regions of the bottom glyph without worrying about overdraw (hairy pixels) and simplifies later clipping.
    LayoutRect clipBounds = info.rect;
    clipBounds.shiftXEdgeTo(from.x());
    clipBounds.shiftMaxXEdgeTo(to.x());
    info.context().clip(clipBounds);

    // Trimming may remove up to two pixels from the left of the extender glyph, so we move it left by two pixels.
    float offsetToGlyphLeft = -2;
    LayoutPoint glyphOrigin = LayoutPoint(from.x() + offsetToGlyphLeft, std::min(from.y(), to.y()) + m_stretchHeightAboveBaseline);
    FloatRect lastPaintedGlyphRect(from, FloatSize());

    while (lastPaintedGlyphRect.maxX() < to.x()) {
        lastPaintedGlyphRect = paintGlyph(info, m_assembly.extension, glyphOrigin, TrimLeftAndRight);
        glyphOrigin.setX(glyphOrigin.x() + lastPaintedGlyphRect.width());

        // There's a chance that if the font size is small enough the glue glyph has been reduced to an empty rectangle
        // with trimming. In that case we just draw nothing.
        if (lastPaintedGlyphRect.isEmpty())
            break;
    }
}

void RenderMathMLOperator::paint(PaintInfo& info, const LayoutPoint& paintOffset)
{
    RenderMathMLToken::paint(info, paintOffset);

    if (info.context().paintingDisabled() || info.phase != PaintPhaseForeground || style().visibility() != VISIBLE || m_stretchType == StretchType::Unstretched)
        return;

    GraphicsContextStateSaver stateSaver(info.context());
    info.context().setFillColor(style().visitedDependentColor(CSSPropertyColor));

    if (m_stretchType == StretchType::SizeVariant) {
        ASSERT(m_variant.isValid());
        GlyphBuffer buffer;
        buffer.add(m_variant.glyph, m_variant.font, advanceWidthForGlyph(m_variant));
        LayoutPoint operatorTopLeft = ceiledIntPoint(paintOffset + location());
        FloatRect glyphBounds = boundsForGlyph(m_variant);
        LayoutPoint operatorOrigin(operatorTopLeft.x(), operatorTopLeft.y() - glyphBounds.y());
        info.context().drawGlyphs(style().fontCascade(), *m_variant.font, buffer, 0, 1, operatorOrigin);
        return;
    }

    if (m_isVertical)
        paintVerticalGlyphAssembly(info, paintOffset);
    else
        paintHorizontalGlyphAssembly(info, paintOffset);
}

void RenderMathMLOperator::paintVerticalGlyphAssembly(PaintInfo& info, const LayoutPoint& paintOffset)
{
    ASSERT(m_isVertical);
    ASSERT(m_stretchType == StretchType::GlyphAssembly);
    ASSERT(m_assembly.topOrRight.isValid());
    ASSERT(m_assembly.bottomOrLeft.isValid());

    // We are positioning the glyphs so that the edge of the tight glyph bounds line up exactly with the edges of our paint box.
    LayoutPoint operatorTopLeft = paintOffset + location();
    operatorTopLeft.move(style().isLeftToRightDirection() ? m_leadingSpace : m_trailingSpace, 0);
    operatorTopLeft = ceiledIntPoint(operatorTopLeft);
    FloatRect topGlyphBounds = boundsForGlyph(m_assembly.topOrRight);
    LayoutPoint topGlyphOrigin(operatorTopLeft.x(), operatorTopLeft.y() - topGlyphBounds.y());
    LayoutRect topGlyphPaintRect = paintGlyph(info, m_assembly.topOrRight, topGlyphOrigin, TrimBottom);

    FloatRect bottomGlyphBounds = boundsForGlyph(m_assembly.bottomOrLeft);
    LayoutPoint bottomGlyphOrigin(operatorTopLeft.x(), operatorTopLeft.y() + offsetHeight() - (bottomGlyphBounds.height() + bottomGlyphBounds.y()));
    LayoutRect bottomGlyphPaintRect = paintGlyph(info, m_assembly.bottomOrLeft, bottomGlyphOrigin, TrimTop);

    if (m_assembly.middle.isValid()) {
        // Center the glyph origin between the start and end glyph paint extents. Then shift it half the paint height toward the bottom glyph.
        FloatRect middleGlyphBounds = boundsForGlyph(m_assembly.middle);
        LayoutPoint middleGlyphOrigin(operatorTopLeft.x(), topGlyphOrigin.y());
        middleGlyphOrigin.moveBy(LayoutPoint(0, (bottomGlyphPaintRect.y() - topGlyphPaintRect.maxY()) / 2.0));
        middleGlyphOrigin.moveBy(LayoutPoint(0, middleGlyphBounds.height() / 2.0));

        LayoutRect middleGlyphPaintRect = paintGlyph(info, m_assembly.middle, middleGlyphOrigin, TrimTopAndBottom);
        fillWithVerticalExtensionGlyph(info, topGlyphPaintRect.minXMaxYCorner(), middleGlyphPaintRect.minXMinYCorner());
        fillWithVerticalExtensionGlyph(info, middleGlyphPaintRect.minXMaxYCorner(), bottomGlyphPaintRect.minXMinYCorner());
    } else
        fillWithVerticalExtensionGlyph(info, topGlyphPaintRect.minXMaxYCorner(), bottomGlyphPaintRect.minXMinYCorner());
}

void RenderMathMLOperator::paintHorizontalGlyphAssembly(PaintInfo& info, const LayoutPoint& paintOffset)
{
    ASSERT(!m_isVertical);
    ASSERT(m_stretchType == StretchType::GlyphAssembly);
    ASSERT(m_assembly.bottomOrLeft.isValid());
    ASSERT(m_assembly.topOrRight.isValid());

    // We are positioning the glyphs so that the edge of the tight glyph bounds line up exactly with the edges of our paint box.
    LayoutPoint operatorTopLeft = paintOffset + location();
    operatorTopLeft.move(m_leadingSpace, 0);
    operatorTopLeft = ceiledIntPoint(operatorTopLeft);
    LayoutPoint leftGlyphOrigin(operatorTopLeft.x(), operatorTopLeft.y() + m_stretchHeightAboveBaseline);
    LayoutRect leftGlyphPaintRect = paintGlyph(info, m_assembly.bottomOrLeft, leftGlyphOrigin, TrimRight);

    FloatRect rightGlyphBounds = boundsForGlyph(m_assembly.topOrRight);
    LayoutPoint rightGlyphOrigin(operatorTopLeft.x() + offsetWidth() - rightGlyphBounds.width(), operatorTopLeft.y() + m_stretchHeightAboveBaseline);
    LayoutRect rightGlyphPaintRect = paintGlyph(info, m_assembly.topOrRight, rightGlyphOrigin, TrimLeft);

    if (m_assembly.middle.isValid()) {
        // Center the glyph origin between the start and end glyph paint extents.
        LayoutPoint middleGlyphOrigin(operatorTopLeft.x(), leftGlyphOrigin.y());
        middleGlyphOrigin.moveBy(LayoutPoint((rightGlyphPaintRect.x() - leftGlyphPaintRect.maxX()) / 2.0, 0));
        LayoutRect middleGlyphPaintRect = paintGlyph(info, m_assembly.middle, middleGlyphOrigin, TrimLeftAndRight);
        fillWithHorizontalExtensionGlyph(info, leftGlyphPaintRect.maxXMinYCorner(), middleGlyphPaintRect.minXMinYCorner());
        fillWithHorizontalExtensionGlyph(info, middleGlyphPaintRect.maxXMinYCorner(), rightGlyphPaintRect.minXMinYCorner());
    } else
        fillWithHorizontalExtensionGlyph(info, leftGlyphPaintRect.maxXMinYCorner(), rightGlyphPaintRect.minXMinYCorner());
}

void RenderMathMLOperator::paintChildren(PaintInfo& paintInfo, const LayoutPoint& paintOffset, PaintInfo& paintInfoForChild, bool usePrintRect)
{
    // We skip painting for invisible operators too to avoid some "missing character" glyph to appear if appropriate math fonts are not available.
    if (m_stretchType != StretchType::Unstretched || isInvisibleOperator())
        return;
    RenderMathMLToken::paintChildren(paintInfo, paintOffset, paintInfoForChild, usePrintRect);
}

LayoutUnit RenderMathMLOperator::trailingSpaceError()
{
    const auto& primaryFont = style().fontCascade().primaryFont();
    if (!primaryFont.mathData())
        return 0;

    // For OpenType MATH font, the layout is based on RenderMathOperator for which the preferred width is sometimes overestimated (bug https://bugs.webkit.org/show_bug.cgi?id=130326).
    // Hence we determine the error in the logical width with respect to the actual width of the glyph(s) used to paint the operator.
    LayoutUnit width = logicalWidth();

    if (m_stretchType == StretchType::Unstretched) {
        GlyphData data = style().fontCascade().glyphDataForCharacter(textContent(), !style().isLeftToRightDirection());
        return width - advanceWidthForGlyph(data);
    }

    if (m_stretchType == StretchType::SizeVariant)
        return width - advanceWidthForGlyph(m_variant);

    float assemblyWidth = advanceWidthForGlyph(m_assembly.topOrRight);
    assemblyWidth = std::max(assemblyWidth, advanceWidthForGlyph(m_assembly.bottomOrLeft));
    assemblyWidth = std::max(assemblyWidth, advanceWidthForGlyph(m_assembly.extension));
    if (m_assembly.middle.isValid())
        assemblyWidth = std::max(assemblyWidth, advanceWidthForGlyph(m_assembly.middle));
    return width - assemblyWidth;
}

}

#endif
