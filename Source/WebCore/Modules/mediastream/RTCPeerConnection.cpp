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
#include "MediaEndpointConfiguration.h"
#include "MediaEndpointConfigurationConversions.h"
#include "MediaEndpointInit.h"
#include "MediaStream.h"
#include "MediaStreamTrack.h"
#include "PeerMediaDescription.h"
#include "RTCConfiguration.h"
#include "RTCDataChannel.h"
#include "RTCIceCandidate.h"
#include "RTCIceCandidateEvent.h"
#include "RTCOfferAnswerOptions.h"
#include "RTCRtpReceiver.h"
#include "RTCRtpSender.h"
#include "RTCSessionDescription.h"
#include <wtf/MainThread.h>

namespace WebCore {

static RefPtr<MediaEndpointInit> createMediaEndpointInit(RTCConfiguration& rtcConfig)
{
    Vector<RefPtr<IceServerInfo>> iceServers;
    for (auto& server : rtcConfig.iceServers())
        iceServers.append(IceServerInfo::create(server->urls(), server->credential(), server->username()));

    return MediaEndpointInit::create(iceServers, rtcConfig.iceTransportPolicy(), rtcConfig.bundlePolicy());
}

PassRefPtr<RTCPeerConnection> RTCPeerConnection::create(ScriptExecutionContext& context, const Dictionary& rtcConfiguration, ExceptionCode& ec)
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
    return peerConnection.release();
}

RTCPeerConnection::RTCPeerConnection(ScriptExecutionContext& context, PassRefPtr<RTCConfiguration> configuration, ExceptionCode& ec)
    : ActiveDOMObject(&context)
    , m_signalingState(SignalingStateStable)
    , m_iceGatheringState(IceGatheringStateNew)
    , m_iceConnectionState(IceConnectionStateNew)
    , m_scheduledEventTimer(*this, &RTCPeerConnection::scheduledEventTimerFired)
    , m_configuration(configuration)
    , m_stopped(false)
{
    printf("-> RTCConfiguration::RTCConfiguration\n");

    Document& document = downcast<Document>(context);

    if (!document.frame()) {
        ec = NOT_SUPPORTED_ERR;
        return;
    }

    m_mediaEndpoint = MediaEndpoint::create(this);
    if (!m_mediaEndpoint) {
        ec = NOT_SUPPORTED_ERR;
        return;
    }

    m_mediaEndpoint->setConfiguration(createMediaEndpointInit(*m_configuration));
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
    if (m_signalingState == SignalingStateClosed) {
        ec = INVALID_STATE_ERR;
        return nullptr;
    }

    if (m_senderSet.contains(track->id())) {
        // FIXME: Spec says InvalidParameter
        ec = INVALID_MODIFICATION_ERR;
        return nullptr;
    }

    RefPtr<RTCRtpSender> sender = RTCRtpSender::create(WTF::move(track), stream->id());
    m_senderSet.add(track->id(), sender);

    return sender;
}

void RTCPeerConnection::removeTrack(RTCRtpSender* sender, ExceptionCode& ec)
{
    if (m_signalingState == SignalingStateClosed) {
        ec = INVALID_STATE_ERR;
        return;
    }

    if (!m_senderSet.remove(sender->track()->id()))
        return;

    // FIXME: Mark connection as needing negotiation.
}

static void updateMediaDescriptionsWithTracks(const Vector<RefPtr<PeerMediaDescription>>& mediaDescriptions, Vector<MediaStreamTrack*>& tracks)
{
    // Remove any track elements from tracks that are already represented by a media description
    // and mark media descriptions that don't have a track (anymore) as "available".
    for (auto& mdesc : mediaDescriptions) {
        const String& mdescTrackId = mdesc->mediaStreamTrackId();
        bool foundTrack = tracks.removeFirstMatching([mdescTrackId](const MediaStreamTrack* track) -> bool {
            return track->id() == mdescTrackId;
        });
        if (!foundTrack) {
            mdesc->setMediaStreamId(emptyString());
            mdesc->setMediaStreamTrackId(emptyString());
        }
    }

    // Remove any track elements from tracks that can be matched (by type) to an available media
    // description. Media descriptions that don't get a local (sending) track is marked receive only.
    for (auto& mdesc : mediaDescriptions) {
        if (mdesc->mediaStreamTrackId() != emptyString())
            continue;

        MediaStreamTrack* track = nullptr;
        for (auto t : tracks) {
            if (t->kind() == mdesc->type()) {
                track = t;
                break;
            }
        }

        if (track) {
            mdesc->setMediaStreamId("fix me");
            mdesc->setMediaStreamTrackId(track->id());
            mdesc->setMode("sendrecv");
            tracks.removeFirst(track);
        } else
            mdesc->setMode("recvonly");
    }
}

void RTCPeerConnection::createOffer(const Dictionary& offerOptions, OfferAnswerResolveCallback resolveCallback, RejectCallback rejectCallback, ExceptionCode& ec)
{
    if (m_signalingState == SignalingStateClosed) {
        ec = INVALID_STATE_ERR;
        return;
    }

    RefPtr<RTCOfferOptions> options = RTCOfferOptions::create(offerOptions, ec);
    if (ec) {
        callOnMainThread([rejectCallback] {
            RefPtr<DOMError> error = DOMError::create("Invalid createOffer argument.");
            rejectCallback(error.get());
        });
        return;
    }

    RefPtr<MediaEndpointConfiguration> configurationSnapshot = m_localConfiguration ?
        MediaEndpointConfigurationConversions::fromJSON(MediaEndpointConfigurationConversions::toJSON(m_localConfiguration.get())) : MediaEndpointConfiguration::create();

    Vector<MediaStreamTrack*> localTracks;
    for (auto sender : m_senderSet.values())
        localTracks.append(sender->track());

    updateMediaDescriptionsWithTracks(configurationSnapshot->mediaDescriptions(), localTracks);

    // Add media descriptions for remaining tracks.
    for (auto track : localTracks) {
        RefPtr<PeerMediaDescription> mediaDescription = PeerMediaDescription::create();

        mediaDescription->setMediaStreamId("fix me");
        mediaDescription->setMediaStreamTrackId(track->id());
        mediaDescription->setType(track->kind());
        mediaDescription->setMode("sendrecv");
        // FIXME: payloads
        mediaDescription->setRtcpMux(true);
        mediaDescription->setDtlsSetup("actpass");

        configurationSnapshot->addMediaDescription(WTF::move(mediaDescription));
    }

    int extraMediaDescriptionCount = options->offerToReceiveAudio() + options->offerToReceiveVideo();
    for (int i = 0; i < extraMediaDescriptionCount; ++i) {
        bool audioType = i < options->offerToReceiveAudio();
        RefPtr<PeerMediaDescription> mediaDescription = PeerMediaDescription::create();

        mediaDescription->setType(audioType ? "audio" : "video");
        mediaDescription->setMode("recvonly");
        // FIXME: payloads
        mediaDescription->setRtcpMux(true);
        mediaDescription->setDtlsSetup("actpass");

        configurationSnapshot->addMediaDescription(WTF::move(mediaDescription));
    }

    RefPtr<RTCSessionDescription> offer = RTCSessionDescription::create("offer", MediaEndpointConfigurationConversions::toJSON(configurationSnapshot.get()));
    resolveCallback(WTF::move(offer));
}

void RTCPeerConnection::createAnswer(const Dictionary& answerOptions, OfferAnswerResolveCallback, RejectCallback rejectCallback, ExceptionCode& ec)
{
    if (m_signalingState == SignalingStateClosed) {
        ec = INVALID_STATE_ERR;
        return;
    }

    RefPtr<RTCAnswerOptions> options = RTCAnswerOptions::create(answerOptions, ec);
    if (ec) {
        callOnMainThread([rejectCallback] {
            RefPtr<DOMError> error = DOMError::create("Invalid createAnswer argument.");
            rejectCallback(error.get());
        });
        return;
    }

    // TODO
}

void RTCPeerConnection::setLocalDescription(RTCSessionDescription* description, VoidResolveCallback resolveCallback, RejectCallback rejectCallback, ExceptionCode& ec)
{
    if (m_signalingState == SignalingStateClosed) {
        ec = INVALID_STATE_ERR;
        return;
    }

    DescriptionType descriptionType = parseDescriptionType(description->type());

    SignalingState targetState = targetSignalingState(SetterTypeLocal, descriptionType);
    if (targetState == SignalingStateInvalid) {
        callOnMainThread([rejectCallback] {
            // FIXME: Error type?
            RefPtr<DOMError> error = DOMError::create("InvalidSessionDescriptionError");
            rejectCallback(error.get());
        });
        return;
    }

    unsigned previousNumberOfMediaDescriptions = m_localConfiguration ? m_localConfiguration->mediaDescriptions().size() : 0;

    m_localConfiguration = MediaEndpointConfigurationConversions::fromJSON(description->sdp());
    m_localConfigurationType = description->type();

    bool hasNewMediaDescriptions = m_localConfiguration->mediaDescriptions().size() > previousNumberOfMediaDescriptions;
    bool isInitiator = descriptionType == DescriptionTypeOffer;

    RefPtr<RTCPeerConnection> protectedThis(this);
    m_completeSetLocalDescription = [targetState, resolveCallback, protectedThis]() mutable {
        protectedThis->m_signalingState = targetState;
        resolveCallback();
    };

    if (hasNewMediaDescriptions)
        m_mediaEndpoint->prepareToReceive(m_localConfiguration.get(), isInitiator);

    if (m_remoteConfiguration)
        m_mediaEndpoint->prepareToSend(m_remoteConfiguration.get(), isInitiator);

    // FIXME: Temporary solution
    callOnMainThread(m_completeSetLocalDescription);
}

RefPtr<RTCSessionDescription> RTCPeerConnection::localDescription() const
{
    if (!m_localConfiguration)
        return nullptr;

    return RTCSessionDescription::create(MediaEndpointConfigurationConversions::toJSON(m_localConfiguration.get()), m_localConfigurationType);
}

void RTCPeerConnection::setRemoteDescription(RTCSessionDescription* description, VoidResolveCallback resolveCallback, RejectCallback rejectCallback, ExceptionCode& ec)
{
    if (m_signalingState == SignalingStateClosed) {
        ec = INVALID_STATE_ERR;
        return;
    }

    DescriptionType descriptionType = parseDescriptionType(description->type());

    SignalingState targetState = targetSignalingState(SetterTypeRemote, descriptionType);
    if (targetState == SignalingStateInvalid) {
        callOnMainThread([rejectCallback] {
            // FIXME: Error type?
            RefPtr<DOMError> error = DOMError::create("InvalidSessionDescriptionError");
            rejectCallback(error.get());
        });
        return;
    }

    RefPtr<RTCPeerConnection> protectedThis(this);
    callOnMainThread([targetState, resolveCallback, protectedThis]() mutable {
        protectedThis->m_signalingState = targetState;
        resolveCallback();
    });
}

RefPtr<RTCSessionDescription> RTCPeerConnection::remoteDescription() const
{
    if (!m_remoteConfiguration)
        return nullptr;

    return RTCSessionDescription::create(MediaEndpointConfigurationConversions::toJSON(m_remoteConfiguration.get()), m_remoteConfigurationType);
}

void RTCPeerConnection::updateIce(const Dictionary& rtcConfiguration, ExceptionCode& ec)
{
    if (m_signalingState == SignalingStateClosed) {
        ec = INVALID_STATE_ERR;
        return;
    }

    m_configuration = RTCConfiguration::create(rtcConfiguration, ec);
    if (ec)
        return;

    // FIXME: updateIce() might be renamed to setConfiguration(). It's also possible
    // that its current behavior with update deltas will change.
    m_mediaEndpoint->setConfiguration(createMediaEndpointInit(*m_configuration));
}

void RTCPeerConnection::addIceCandidate(RTCIceCandidate* iceCandidate, VoidResolveCallback resolveCallback, RejectCallback rejectCallback, ExceptionCode& ec)
{
    if (m_signalingState == SignalingStateClosed) {
        ec = INVALID_STATE_ERR;
        return;
    }

    RefPtr<IceCandidate> candidate = MediaEndpointConfigurationConversions::iceCandidateFromJSON(iceCandidate->candidate());
    if (!candidate) {
        rejectCallback(DOMError::create("FIXME: bad candidate"));
        return;
    }

    printf("RTCPeerConnection::addIceCandidate: candidate: type %s, foundation: %s, componentId: %d, transport: %s\n",
        candidate->type().ascii().data(), candidate->foundation().ascii().data(), candidate->componentId(), candidate->transport().ascii().data());

    m_mediaEndpoint->addRemoteCandidate(candidate.get());

    resolveCallback();
}

String RTCPeerConnection::signalingState() const
{
    switch (m_signalingState) {
    case SignalingStateStable:
        return ASCIILiteral("stable");
    case SignalingStateHaveLocalOffer:
        return ASCIILiteral("have-local-offer");
    case SignalingStateHaveRemoteOffer:
        return ASCIILiteral("have-remote-offer");
    case SignalingStateHaveLocalPrAnswer:
        return ASCIILiteral("have-local-pranswer");
    case SignalingStateHaveRemotePrAnswer:
        return ASCIILiteral("have-remote-pranswer");
    case SignalingStateClosed:
        return ASCIILiteral("closed");
    case SignalingStateInvalid:
        break;
    }

    ASSERT_NOT_REACHED();
    return String();
}

String RTCPeerConnection::iceGatheringState() const
{
    switch (m_iceGatheringState) {
    case IceGatheringStateNew:
        return ASCIILiteral("new");
    case IceGatheringStateGathering:
        return ASCIILiteral("gathering");
    case IceGatheringStateComplete:
        return ASCIILiteral("complete");
    }

    ASSERT_NOT_REACHED();
    return String();
}

String RTCPeerConnection::iceConnectionState() const
{
    switch (m_iceConnectionState) {
    case IceConnectionStateNew:
        return ASCIILiteral("new");
    case IceConnectionStateChecking:
        return ASCIILiteral("checking");
    case IceConnectionStateConnected:
        return ASCIILiteral("connected");
    case IceConnectionStateCompleted:
        return ASCIILiteral("completed");
    case IceConnectionStateFailed:
        return ASCIILiteral("failed");
    case IceConnectionStateDisconnected:
        return ASCIILiteral("disconnected");
    case IceConnectionStateClosed:
        return ASCIILiteral("closed");
    }

    ASSERT_NOT_REACHED();
    return String();
}

RTCConfiguration* RTCPeerConnection::getConfiguration() const
{
    return m_configuration.get();
}

void RTCPeerConnection::getStats(PassRefPtr<RTCStatsCallback>, PassRefPtr<RTCPeerConnectionErrorCallback>, PassRefPtr<MediaStreamTrack>)
{
}

PassRefPtr<RTCDataChannel> RTCPeerConnection::createDataChannel(String, const Dictionary&, ExceptionCode& ec)
{
    if (m_signalingState == SignalingStateClosed) {
        ec = INVALID_STATE_ERR;
        return nullptr;
    }

    return nullptr;
}

void RTCPeerConnection::close()
{
    if (m_signalingState == SignalingStateClosed)
        return;

    m_mediaEndpoint->stop();

    m_signalingState = SignalingStateClosed;
}

void RTCPeerConnection::gotSendSSRC(unsigned, const String&, const String&)
{
}

void RTCPeerConnection::gotDtlsFingerprint(unsigned, const String&, const String&)
{
}

void RTCPeerConnection::gotIceCandidate(unsigned mdescIndex, RefPtr<IceCandidate>&& candidate)
{
    printf("-> RTCPeerConnection::gotIceCandidate\n");
    printf("is context thread: %d\n", scriptExecutionContext()->isContextThread());

    ASSERT(scriptExecutionContext()->isContextThread());

    String candidateString = MediaEndpointConfigurationConversions::iceCandidateToJSON(candidate.get());
    RefPtr<RTCIceCandidate> iceCandidate = RTCIceCandidate::create(candidateString, "", mdescIndex);
    scheduleDispatchEvent(RTCIceCandidateEvent::create(false, false, WTF::move(iceCandidate)));
}

void RTCPeerConnection::doneGatheringCandidates(unsigned)
{
    printf("-> RTCPeerConnection::doneGatheringCandidates\n");
    printf("is context thread: %d\n", scriptExecutionContext()->isContextThread());

    ASSERT(scriptExecutionContext()->isContextThread());

    scheduleDispatchEvent(RTCIceCandidateEvent::create(false, false, 0));
}

void RTCPeerConnection::gotRemoteSource(unsigned, RefPtr<RealTimeMediaSource>&&)
{
}

void RTCPeerConnection::stop()
{
    if (m_stopped)
        return;

    m_stopped = true;
    m_iceConnectionState = IceConnectionStateClosed;
    m_signalingState = SignalingStateClosed;
}

RTCPeerConnection::SignalingState RTCPeerConnection::targetSignalingState(SetterType setter, DescriptionType description) const
{
    // "map" describing the valid state transitions
    switch (m_signalingState) {
    case SignalingStateStable:
        if (setter == SetterTypeLocal && description == DescriptionTypeOffer)
            return SignalingStateHaveLocalOffer;
        if (setter == SetterTypeRemote && description == DescriptionTypeOffer)
            return SignalingStateHaveRemoteOffer;
        break;
    case SignalingStateHaveLocalOffer:
        if (setter == SetterTypeLocal && description == DescriptionTypeOffer)
            return SignalingStateHaveLocalOffer;
        if (setter == SetterTypeRemote && description == DescriptionTypeAnswer)
            return SignalingStateStable;
        break;
    case SignalingStateHaveRemoteOffer:
        if (setter == SetterTypeLocal && description == DescriptionTypeAnswer)
            return SignalingStateStable;
        if (setter == SetterTypeRemote && description == DescriptionTypeOffer)
            return SignalingStateHaveRemoteOffer;
        break;
    default:
        break;
    };
    return SignalingStateInvalid;
}

RTCPeerConnection::DescriptionType RTCPeerConnection::parseDescriptionType(const String& typeName) const
{
    if (typeName == "offer")
        return DescriptionTypeOffer;
    if (typeName == "pranswer")
        return DescriptionTypePranswer;

    ASSERT(typeName == "answer");
    return DescriptionTypeAnswer;
}

const char* RTCPeerConnection::activeDOMObjectName() const
{
    return "RTCPeerConnection";
}

bool RTCPeerConnection::canSuspend() const
{
    // FIXME: We should try and do better here.
    return false;
}

void RTCPeerConnection::changeSignalingState(SignalingState signalingState)
{
    if (m_signalingState != SignalingStateClosed && m_signalingState != signalingState) {
        m_signalingState = signalingState;
        scheduleDispatchEvent(Event::create(eventNames().signalingstatechangeEvent, false, false));
    }
}

void RTCPeerConnection::changeIceGatheringState(IceGatheringState iceGatheringState)
{
    m_iceGatheringState = iceGatheringState;
}

void RTCPeerConnection::changeIceConnectionState(IceConnectionState iceConnectionState)
{
    if (m_iceConnectionState != IceConnectionStateClosed && m_iceConnectionState != iceConnectionState) {
        m_iceConnectionState = iceConnectionState;
        scheduleDispatchEvent(Event::create(eventNames().iceconnectionstatechangeEvent, false, false));
    }
}

void RTCPeerConnection::scheduleDispatchEvent(PassRefPtr<Event> event)
{
    m_scheduledEvents.append(event);

    if (!m_scheduledEventTimer.isActive())
        m_scheduledEventTimer.startOneShot(0);
}

void RTCPeerConnection::scheduledEventTimerFired()
{
    if (m_stopped)
        return;

    Vector<RefPtr<Event>> events;
    events.swap(m_scheduledEvents);

    Vector<RefPtr<Event>>::iterator it = events.begin();
    for (; it != events.end(); ++it)
        dispatchEvent((*it).release());

    events.clear();
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
