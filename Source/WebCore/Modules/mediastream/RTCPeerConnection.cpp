/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2013 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (C) 2015 Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of Google Inc. nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
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

#if ENABLE(MEDIA_STREAM)

#include "RTCPeerConnection.h"

#include "DOMError.h"
#include "Document.h"
#include "Event.h"
#include "ExceptionCode.h"
#include "Frame.h"
#include "JSDOMError.h"
#include "MediaStream.h"
#include "MediaStreamTrack.h"
#include "RTCConfiguration.h"
#include "RTCDataChannel.h"
#include "RTCIceCandidate.h"
#include "RTCIceCandidateEvent.h"
#include "RTCOfferAnswerOptions.h"
#include "RTCRtpReceiver.h"
#include "RTCRtpSender.h"
#include "RTCSessionDescription.h"
#include "RTCTrackEvent.h"
#include <wtf/MainThread.h>
#include <wtf/text/Base64.h>

namespace WebCore {

using namespace PeerConnection;
using namespace PeerConnectionStates;

RefPtr<RTCPeerConnection> RTCPeerConnection::create(ScriptExecutionContext& context, const Dictionary& rtcConfiguration, ExceptionCode& ec)
{
    printf("-> RTCConfiguration::create\n");

    RefPtr<RTCConfiguration> configuration = RTCConfiguration::create(rtcConfiguration, ec);
    if (ec)
        return nullptr;

    printf("RTCPeerConnection: config was ok\n");

    RefPtr<RTCPeerConnection> peerConnection = adoptRef(new RTCPeerConnection(context, configuration.release(), ec));
    peerConnection->suspendIfNeeded();
    if (ec)
        return nullptr;

    printf("RTCPeerConnection: before release\n");
    return peerConnection;
}

RTCPeerConnection::RTCPeerConnection(ScriptExecutionContext& context, PassRefPtr<RTCConfiguration> configuration, ExceptionCode& ec)
    : ActiveDOMObject(&context)
    , m_signalingState(SignalingState::Stable)
    , m_iceGatheringState(IceGatheringState::New)
    , m_iceConnectionState(IceConnectionState::New)
    , m_configuration(configuration)
    , m_stopped(false)
{
    printf("-> RTCConfiguration::RTCConfiguration\n");

    Document& document = downcast<Document>(context);

    if (!document.frame()) {
        ec = NOT_SUPPORTED_ERR;
        return;
    }

    m_backend = PeerConnectionBackend::create(this);
    if (!m_backend) {
        ec = NOT_SUPPORTED_ERR;
        return;
    }

    m_backend->setConfiguration(*m_configuration);
}

RTCPeerConnection::~RTCPeerConnection()
{
    stop();
}

Vector<RefPtr<RTCRtpSender>> RTCPeerConnection::getSenders() const
{
    Vector<RefPtr<RTCRtpSender>> senders;
    for (auto& sender : m_senderSet.values())
        senders.append(sender);

    return senders;
}

Vector<RefPtr<RTCRtpReceiver>> RTCPeerConnection::getReceivers() const
{
    Vector<RefPtr<RTCRtpReceiver>> receivers;
    for (auto& receiver : m_receiverSet.values())
        receivers.append(receiver);

    return receivers;
}

RefPtr<RTCRtpSender> RTCPeerConnection::addTrack(RefPtr<MediaStreamTrack>&& track, const MediaStream* stream, ExceptionCode& ec)
{
    if (m_signalingState == SignalingState::Closed) {
        ec = INVALID_STATE_ERR;
        return nullptr;
    }

    if (m_senderSet.contains(track->id())) {
        // FIXME: Spec says InvalidParameter
        ec = INVALID_MODIFICATION_ERR;
        return nullptr;
    }

    const String& trackId = track->id();
    RefPtr<RTCRtpSender> sender = RTCRtpSender::create(WTF::move(track), stream->id());
    m_senderSet.add(trackId, sender);

    m_backend->markAsNeedingNegotiation();

    return sender;
}

void RTCPeerConnection::removeTrack(RTCRtpSender* sender, ExceptionCode& ec)
{
    if (m_signalingState == SignalingState::Closed) {
        ec = INVALID_STATE_ERR;
        return;
    }

    if (!m_senderSet.remove(sender->track()->id()))
        return;

    m_backend->markAsNeedingNegotiation();
}

void RTCPeerConnection::createOffer(const Dictionary& offerOptions, SessionDescriptionPromise&& promise)
{
    if (m_signalingState == SignalingState::Closed) {
        promise.reject(DOMError::create("InvalidStateError"));
        return;
    }

    ExceptionCode ec = 0;
    RefPtr<RTCOfferOptions> options = RTCOfferOptions::create(offerOptions, ec);
    if (ec) {
        promise.reject(DOMError::create("Invalid createOffer argument"));
        return;
    }
    ASSERT(options);

    m_backend->createOffer(*options, WTF::move(promise));
}

void RTCPeerConnection::createAnswer(const Dictionary& answerOptions, SessionDescriptionPromise&& promise)
{
    if (m_signalingState == SignalingState::Closed) {
        promise.reject(DOMError::create("InvalidStateError"));
        return;
    }

    ExceptionCode ec = 0;
    RefPtr<RTCAnswerOptions> options = RTCAnswerOptions::create(answerOptions, ec);
    if (ec) {
        promise.reject(DOMError::create("Invalid createAnswer argument"));
        return;
    }

    if (!remoteDescription()) {
        // FIXME: Error type?
        promise.reject(DOMError::create("InvalidStateError (no remote description)"));
        return;
    }

    m_backend->createAnswer(*options, WTF::move(promise));
}

void RTCPeerConnection::setLocalDescription(RTCSessionDescription* description, PeerConnection::VoidPromise&& promise)
{
    if (m_signalingState == SignalingState::Closed) {
        promise.reject(DOMError::create("InvalidStateError"));
        return;
    }

    m_backend->setLocalDescription(description, WTF::move(promise));
}

RefPtr<RTCSessionDescription> RTCPeerConnection::localDescription() const
{
    return m_backend->localDescription();
}

RefPtr<RTCSessionDescription> RTCPeerConnection::currentLocalDescription() const
{
    return m_backend->currentLocalDescription();
}

RefPtr<RTCSessionDescription> RTCPeerConnection::pendingLocalDescription() const
{
    return m_backend->pendingLocalDescription();
}

void RTCPeerConnection::setRemoteDescription(RTCSessionDescription* description, PeerConnection::VoidPromise&& promise)
{
    if (m_signalingState == SignalingState::Closed) {
        promise.reject(DOMError::create("InvalidStateError"));
        return;
    }

    m_backend->setRemoteDescription(description, WTF::move(promise));
}

RefPtr<RTCSessionDescription> RTCPeerConnection::remoteDescription() const
{
    return m_backend->remoteDescription();
}

RefPtr<RTCSessionDescription> RTCPeerConnection::currentRemoteDescription() const
{
    return m_backend->currentRemoteDescription();
}

RefPtr<RTCSessionDescription> RTCPeerConnection::pendingRemoteDescription() const
{
    return m_backend->pendingRemoteDescription();
}

void RTCPeerConnection::addIceCandidate(RTCIceCandidate* rtcCandidate, VoidPromise&& promise)
{
    if (m_signalingState == SignalingState::Closed) {
        promise.reject(DOMError::create("InvalidStateError"));
        return;
    }

    m_backend->addIceCandidate(rtcCandidate, WTF::move(promise));
}

String RTCPeerConnection::signalingState() const
{
    switch (m_signalingState) {
    case SignalingState::Stable:
        return ASCIILiteral("stable");
    case SignalingState::HaveLocalOffer:
        return ASCIILiteral("have-local-offer");
    case SignalingState::HaveRemoteOffer:
        return ASCIILiteral("have-remote-offer");
    case SignalingState::HaveLocalPrAnswer:
        return ASCIILiteral("have-local-pranswer");
    case SignalingState::HaveRemotePrAnswer:
        return ASCIILiteral("have-remote-pranswer");
    case SignalingState::Closed:
        return ASCIILiteral("closed");
    }

    ASSERT_NOT_REACHED();
    return String();
}

String RTCPeerConnection::iceGatheringState() const
{
    switch (m_iceGatheringState) {
    case IceGatheringState::New:
        return ASCIILiteral("new");
    case IceGatheringState::Gathering:
        return ASCIILiteral("gathering");
    case IceGatheringState::Complete:
        return ASCIILiteral("complete");
    }

    ASSERT_NOT_REACHED();
    return String();
}

String RTCPeerConnection::iceConnectionState() const
{
    switch (m_iceConnectionState) {
    case IceConnectionState::New:
        return ASCIILiteral("new");
    case IceConnectionState::Checking:
        return ASCIILiteral("checking");
    case IceConnectionState::Connected:
        return ASCIILiteral("connected");
    case IceConnectionState::Completed:
        return ASCIILiteral("completed");
    case IceConnectionState::Failed:
        return ASCIILiteral("failed");
    case IceConnectionState::Disconnected:
        return ASCIILiteral("disconnected");
    case IceConnectionState::Closed:
        return ASCIILiteral("closed");
    }

    ASSERT_NOT_REACHED();
    return String();
}

RTCConfiguration* RTCPeerConnection::getConfiguration() const
{
    return m_configuration.get();
}

void RTCPeerConnection::setConfiguration(const Dictionary& rtcConfiguration, ExceptionCode& ec)
{
    if (m_signalingState == SignalingState::Closed) {
        ec = INVALID_STATE_ERR;
        return;
    }

    m_configuration = RTCConfiguration::create(rtcConfiguration, ec);
    if (ec)
        return;

    m_backend->setConfiguration(*m_configuration);
}

void RTCPeerConnection::getStats(PassRefPtr<RTCStatsCallback>, PassRefPtr<RTCPeerConnectionErrorCallback>, PassRefPtr<MediaStreamTrack>)
{
}

PassRefPtr<RTCDataChannel> RTCPeerConnection::createDataChannel(String, const Dictionary&, ExceptionCode& ec)
{
    if (m_signalingState == SignalingState::Closed) {
        ec = INVALID_STATE_ERR;
        return nullptr;
    }

    return nullptr;
}

void RTCPeerConnection::close()
{
    if (m_signalingState == SignalingState::Closed)
        return;

    m_signalingState = SignalingState::Closed;
}

void RTCPeerConnection::stop()
{
    if (m_stopped)
        return;

    m_stopped = true;
    m_iceConnectionState = IceConnectionState::Closed;
    m_signalingState = SignalingState::Closed;
}

const char* RTCPeerConnection::activeDOMObjectName() const
{
    return "RTCPeerConnection";
}

bool RTCPeerConnection::canSuspendForPageCache() const
{
    // FIXME: We should try and do better here.
    return false;
}

void RTCPeerConnection::setSignalingState(SignalingState newState)
{
    m_signalingState = newState;
}

void RTCPeerConnection::updateIceGatheringState(IceGatheringState newState)
{
    scriptExecutionContext()->postTask([=](ScriptExecutionContext&) {
        m_iceGatheringState = newState;

        // FIXME: add icegatheringstatechange to event names
        dispatchEvent(Event::create(eventNames().statechangeEvent, false, false));
    });
}

void RTCPeerConnection::updateIceConnectionState(IceConnectionState newState)
{
    scriptExecutionContext()->postTask([=](ScriptExecutionContext&) {
        m_iceConnectionState = newState;

        dispatchEvent(Event::create(eventNames().iceconnectionstatechangeEvent, false, false));
    });
}

void RTCPeerConnection::scheduleNegotiationNeededEvent() const
{
    scriptExecutionContext()->postTask([=](ScriptExecutionContext&) {
        if (m_backend->isNegotiationNeeded()) {
            dispatchEvent(Event::create(eventNames().negotiationneededEvent, false, false));
            m_backend->clearNegotiationNeededState();
        }
    });
}

void RTCPeerConnection::fireEvent(PassRefPtr<Event> event)
{
    dispatchEvent(event);
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
