/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
 * Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (C) 2012 Company 100, Inc.
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
#include "ThreadedCoordinatedLayerTreeHost.h"

#if USE(COORDINATED_GRAPHICS_THREADED)

#include "DrawingAreaProxyMessages.h"
#include "NotImplemented.h"
#include "ThreadSafeCoordinatedSurface.h"
#include "WebPage.h"
#include "WebPageProxyMessages.h"
#include "WebProcess.h"
#include <WebCore/CoordinatedGraphicsLayer.h>
#include <WebCore/CoordinatedGraphicsState.h>
#include <WebCore/Frame.h>
#include <WebCore/FrameView.h>
#include <WebCore/GraphicsContext.h>
#include <WebCore/MainFrame.h>
#include <WebCore/Page.h>
#include <wtf/CurrentTime.h>

#if PLATFORM(WPE)
#include "DrawingAreaWPE.h"
#endif

using namespace WebCore;

namespace WebKit {

Ref<ThreadedCoordinatedLayerTreeHost> ThreadedCoordinatedLayerTreeHost::create(WebPage& webPage)
{
    return adoptRef(*new ThreadedCoordinatedLayerTreeHost(webPage));
}

ThreadedCoordinatedLayerTreeHost::~ThreadedCoordinatedLayerTreeHost()
{
}

ThreadedCoordinatedLayerTreeHost::ThreadedCoordinatedLayerTreeHost(WebPage& webPage)
    : LayerTreeHost(webPage)
    , m_notifyAfterScheduledLayerFlush(false)
    , m_isSuspended(false)
    , m_isWaitingForRenderer(false)
    , m_scheduledWhileWaitingForRenderer(false)
    , m_layerFlushTimer(RunLoop::main(), this, &ThreadedCoordinatedLayerTreeHost::performScheduledLayerFlush)
    , m_layerFlushSchedulingEnabled(true)
{
    m_layerFlushTimer.setPriority(G_PRIORITY_HIGH + 30);

    m_coordinator = std::make_unique<CompositingCoordinator>(m_webPage.corePage(), this);
    m_coordinator->createRootLayer(m_webPage.size());

    CoordinatedSurface::setFactory(createCoordinatedSurface);

    m_compositor = ThreadedCompositor::create(this, m_webPage);
    scheduleLayerFlush();
}

RefPtr<CoordinatedSurface> ThreadedCoordinatedLayerTreeHost::createCoordinatedSurface(const IntSize& size, CoordinatedSurface::Flags flags)
{
    return ThreadSafeCoordinatedSurface::create(size, flags);
}

void ThreadedCoordinatedLayerTreeHost::scheduleLayerFlush()
{
    if (!m_layerFlushSchedulingEnabled)
        return;

    if (m_isWaitingForRenderer) {
        m_scheduledWhileWaitingForRenderer = true;
        return;
    }

    if (!m_layerFlushTimer.isActive())
        m_layerFlushTimer.startOneShot(0);
}

void ThreadedCoordinatedLayerTreeHost::setLayerFlushSchedulingEnabled(bool layerFlushingEnabled)
{
    if (m_layerFlushSchedulingEnabled == layerFlushingEnabled)
        return;

    m_layerFlushSchedulingEnabled = layerFlushingEnabled;

    if (m_layerFlushSchedulingEnabled) {
        scheduleLayerFlush();
        return;
    }

    cancelPendingLayerFlush();
}

void ThreadedCoordinatedLayerTreeHost::setShouldNotifyAfterNextScheduledLayerFlush(bool notifyAfterScheduledLayerFlush)
{
    m_notifyAfterScheduledLayerFlush = notifyAfterScheduledLayerFlush;
}

void ThreadedCoordinatedLayerTreeHost::setRootCompositingLayer(WebCore::GraphicsLayer* graphicsLayer)
{
    m_coordinator->setRootCompositingLayer(graphicsLayer);
}

void ThreadedCoordinatedLayerTreeHost::invalidate()
{
    notImplemented();
}

void ThreadedCoordinatedLayerTreeHost::scrollNonCompositedContents(const WebCore::IntRect& rect)
{
    m_compositor->scrollTo(rect.location());
    scheduleLayerFlush();
}

void ThreadedCoordinatedLayerTreeHost::forceRepaint()
{
    scheduleLayerFlush();
}

void ThreadedCoordinatedLayerTreeHost::sizeDidChange(const WebCore::IntSize& newSize)
{
    m_coordinator->sizeDidChange(newSize);
    m_compositor->didChangeContentsSize(newSize);
}

void ThreadedCoordinatedLayerTreeHost::deviceOrPageScaleFactorChanged()
{
    m_coordinator->deviceOrPageScaleFactorChanged();
    m_compositor->setDeviceScaleFactor(m_webPage.deviceScaleFactor());
}

void ThreadedCoordinatedLayerTreeHost::pauseRendering()
{
    m_isSuspended = true;
}

void ThreadedCoordinatedLayerTreeHost::resumeRendering()
{
    m_isSuspended = false;
    scheduleLayerFlush();
}

GraphicsLayerFactory* ThreadedCoordinatedLayerTreeHost::graphicsLayerFactory()
{
    return m_coordinator.get();
}

void ThreadedCoordinatedLayerTreeHost::viewportSizeChanged(const WebCore::IntSize& size)
{
    m_compositor->didChangeViewportSize(size);
}

void ThreadedCoordinatedLayerTreeHost::didChangeViewportProperties(const WebCore::ViewportAttributes& attr)
{
    m_compositor->didChangeViewportAttribute(attr);
}

void ThreadedCoordinatedLayerTreeHost::compositorDidFlushLayers()
{
#if PLATFORM(WPE)
    static_cast<DrawingAreaWPE*>(m_webPage.drawingArea())->layerHostDidFlushLayers();
#else
    static_cast<DrawingAreaImpl*>(m_webPage.drawingArea())->layerHostDidFlushLayers();
#endif
}

void ThreadedCoordinatedLayerTreeHost::didScaleFactorChanged(float scale, const IntPoint& origin)
{
    m_webPage.scalePage(scale, origin);
}

void ThreadedCoordinatedLayerTreeHost::setViewOverlayRootLayer(GraphicsLayer* graphicsLayer)
{
    m_coordinator->setViewOverlayRootLayer(graphicsLayer);
}

#if PLATFORM(GTK)
void ThreadedCoordinatedLayerTreeHost::setNativeSurfaceHandleForCompositing(uint64_t handle)
{
    m_layerTreeContext.contextID = handle;
    m_compositor->setNativeSurfaceHandleForCompositing(handle);
}
#endif

#if ENABLE(REQUEST_ANIMATION_FRAME)
void ThreadedCoordinatedLayerTreeHost::scheduleAnimation()
{
    if (m_isWaitingForRenderer)
        return;

    if (m_layerFlushTimer.isActive())
        return;

    m_layerFlushTimer.startOneShot(m_coordinator->nextAnimationServiceTime());
}
#endif

void ThreadedCoordinatedLayerTreeHost::setVisibleContentsRect(const FloatRect& rect, const FloatPoint& trajectoryVector, float scale)
{
    m_coordinator->setVisibleContentsRect(rect, trajectoryVector);
    if (m_lastScrollPosition != roundedIntPoint(rect.location())) {
        m_lastScrollPosition = roundedIntPoint(rect.location());

        if (!m_webPage.corePage()->mainFrame().view()->useFixedLayout())
            m_webPage.corePage()->mainFrame().view()->notifyScrollPositionChanged(m_lastScrollPosition);
    }

    if (m_lastScaleFactor != scale) {
        m_lastScaleFactor = scale;
        didScaleFactorChanged(m_lastScaleFactor, m_lastScrollPosition);
    }

    scheduleLayerFlush();
}

void ThreadedCoordinatedLayerTreeHost::cancelPendingLayerFlush()
{
    m_layerFlushTimer.stop();
}

void ThreadedCoordinatedLayerTreeHost::performScheduledLayerFlush()
{
    if (m_isSuspended || m_isWaitingForRenderer)
        return;

    m_coordinator->syncDisplayState();
    bool didSync = m_coordinator->flushPendingLayerChanges();

    if (m_notifyAfterScheduledLayerFlush && didSync) {
        compositorDidFlushLayers();
        m_notifyAfterScheduledLayerFlush = false;
    }
}

void ThreadedCoordinatedLayerTreeHost::purgeBackingStores()
{
    m_coordinator->purgeBackingStores();
}

void ThreadedCoordinatedLayerTreeHost::renderNextFrame()
{
    m_isWaitingForRenderer = false;
    bool scheduledWhileWaitingForRenderer = std::exchange(m_scheduledWhileWaitingForRenderer, false);
    m_coordinator->renderNextFrame();

    if (scheduledWhileWaitingForRenderer || m_layerFlushTimer.isActive()) {
        m_layerFlushTimer.stop();
        performScheduledLayerFlush();
    }
}

void ThreadedCoordinatedLayerTreeHost::commitScrollOffset(uint32_t layerID, const IntSize& offset)
{
    m_coordinator->commitScrollOffset(layerID, offset);
}

void ThreadedCoordinatedLayerTreeHost::notifyFlushRequired()
{
    scheduleLayerFlush();
}

void ThreadedCoordinatedLayerTreeHost::commitSceneState(const CoordinatedGraphicsState& state)
{
    m_isWaitingForRenderer = true;
    m_compositor->updateSceneState(state);
}

void ThreadedCoordinatedLayerTreeHost::paintLayerContents(const GraphicsLayer*, GraphicsContext&, const IntRect&)
{
}

#if USE(REQUEST_ANIMATION_FRAME_DISPLAY_MONITOR)
RefPtr<WebCore::DisplayRefreshMonitor> ThreadedCoordinatedLayerTreeHost::createDisplayRefreshMonitor(PlatformDisplayID displayID)
{
    return m_compositor->createDisplayRefreshMonitor(displayID);
}
#endif

} // namespace WebKit

#endif // USE(COORDINATED_GRAPHICS)
