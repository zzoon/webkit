/*
 * Copyright (C) 2014 Igalia S.L.
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
#include "PlatformWebView.h"

#include <WebKit/WKImageCairo.h>
#include <cairo.h>
#include <cstdio>
#include <glib.h>

namespace WTR {

PlatformWebView::PlatformWebView(WKPageConfigurationRef configuration, const TestOptions& options)
    : m_view(WKViewCreate(configuration))
    , m_window(nullptr)
    , m_windowIsKey(true)
    , m_options(options)
{
}

PlatformWebView::~PlatformWebView()
{
}

void PlatformWebView::resizeTo(unsigned, unsigned)
{
}

WKPageRef PlatformWebView::page()
{
    return WKViewGetPage(m_view);
}

void PlatformWebView::focus()
{
}

WKRect PlatformWebView::windowFrame()
{
    return WKRect{ 0, 0, 1, 1 };
}

void PlatformWebView::setWindowFrame(WKRect)
{
}

void PlatformWebView::didInitializeClients()
{
}

void PlatformWebView::addChromeInputField()
{
}

void PlatformWebView::removeChromeInputField()
{
}

void PlatformWebView::setWindowIsKey(bool windowIsKey)
{
    m_windowIsKey = windowIsKey;
}

void PlatformWebView::makeWebViewFirstResponder()
{
}

WKRetainPtr<WKImageRef> PlatformWebView::windowSnapshotImage()
{
    cairo_surface_t* imageSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 800, 600);
    WKRetainPtr<WKImageRef> wkImage = adoptWK(WKImageCreateFromCairoSurface(imageSurface, 0 /* options */));
    cairo_surface_destroy(imageSurface);
    return wkImage;
}

void PlatformWebView::changeWindowScaleIfNeeded(float)
{
}

void PlatformWebView::setNavigationGesturesEnabled(bool)
{
}

bool PlatformWebView::viewSupportsOptions(const TestOptions&) const
{
    return true;
}

void PlatformWebView::forceWindowFramesChanged()
{
}

} // namespace WTR
