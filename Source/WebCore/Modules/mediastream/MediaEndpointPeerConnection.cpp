/*
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
 * 3. Neither the name of Ericsson nor the names of its contributors
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

#if ENABLE(WEB_RTC)
#include "MediaEndpointPeerConnection.h"

#include "DOMError.h"
#include "JSDOMError.h"
#include "JSRTCSessionDescription.h"
#include "MediaEndpointSessionConfiguration.h"
#include "MediaStreamTrack.h"
#include "PeerConnectionBackend.h"
#include "PeerMediaDescription.h"
#include "RTCConfiguration.h"
#include "RTCIceCandidate.h"
#include "RTCIceCandidateEvent.h"
#include "RTCOfferAnswerOptions.h"
#include "RTCRtpReceiver.h"
#include "RTCRtpSender.h"
#include "RTCRtpTransceiver.h"
#include "RTCSessionDescription.h"
#include "RTCTrackEvent.h"
#include "SDPProcessor.h"
#include "UUID.h"
#include <wtf/MainThread.h>
#include <wtf/text/Base64.h>

namespace WebCore {

using namespace PeerConnection;
using namespace PeerConnectionStates;

static std::unique_ptr<PeerConnectionBackend> createMediaEndpointPeerConnection(PeerConnectionBackendClient* client)
{
    return std::unique_ptr<PeerConnectionBackend>(new MediaEndpointPeerConnection(client));
}

CreatePeerConnectionBackend PeerConnectionBackend::create = createMediaEndpointPeerConnection;

class WrappedSessionDescriptionPromise : public RefCounted<WrappedSessionDescriptionPromise> {
public:
    static Ref<WrappedSessionDescriptionPromise> create(SessionDescriptionPromise&& promise)
    {
        return *adoptRef(new WrappedSessionDescriptionPromise(WTFMove(promise)));
    }

    SessionDescriptionPromise& promise() { return m_promise; }

private:
    WrappedSessionDescriptionPromise(SessionDescriptionPromise&& promise)
        : m_promise(WTFMove(promise))
    { }

    SessionDescriptionPromise m_promise;
};

class WrappedVoidPromise : public RefCounted<WrappedVoidPromise> {
public:
    static Ref<WrappedVoidPromise> create(VoidPromise&& promise)
    {
        return *adoptRef(new WrappedVoidPromise(WTFMove(promise)));
    }

    VoidPromise& promise() { return m_promise; }

private:
    WrappedVoidPromise(VoidPromise&& promise)
        : m_promise(WTFMove(promise))
    { }

    VoidPromise m_promise;
};

static String randomString(size_t length)
{
    const size_t size = ceil(length * 3 / 4);
    unsigned char randomValues[size];
    cryptographicallyRandomValues(randomValues, size);
    return base64Encode(randomValues, size);
}

static RefPtr<MediaEndpointConfiguration> createMediaEndpointConfiguration(RTCConfiguration& rtcConfig)
{
    Vector<RefPtr<IceServerInfo>> iceServers;
    for (auto& server : rtcConfig.iceServers())
        iceServers.append(IceServerInfo::create(server->urls(), server->credential(), server->username()));

    return MediaEndpointConfiguration::create(iceServers, rtcConfig.iceTransportPolicy(), rtcConfig.bundlePolicy());
}

MediaEndpointPeerConnection::MediaEndpointPeerConnection(PeerConnectionBackendClient* client)
    : m_client(client)
    , m_sdpProcessor(std::unique_ptr<SDPProcessor>(new SDPProcessor(m_client->scriptExecutionContext())))
    , m_cname(randomString(16))
    , m_iceUfrag(randomString(4))
    , m_icePassword(randomString(22))
{
    m_mediaEndpoint = MediaEndpoint::create(*this);
    ASSERT(m_mediaEndpoint);

    m_defaultAudioPayloads = m_mediaEndpoint->getDefaultAudioPayloads();
    m_defaultVideoPayloads = m_mediaEndpoint->getDefaultVideoPayloads();

    // Tasks (see runTask()) will be deferred until we get the DTLS fingerprint.
    m_mediaEndpoint->generateDtlsInfo();
}

MediaEndpointPeerConnection::~MediaEndpointPeerConnection()
{
}

static size_t indexOfSenderWithTrackId(const RtpSenderVector& senders, const String& trackId)
{
    for (size_t i = 0; i < senders.size(); ++i) {
        if (senders[i]->trackId() == trackId)
            return i;
    }
    return notFound;
}

static RTCRtpTransceiver* matchTransceiver(const RtpTransceiverVector& transceivers, const std::function<bool (RTCRtpTransceiver&)>& matchFunction)
{
    for (auto& transceiver : transceivers) {
        if (matchFunction(*transceiver))
            return transceiver.get();
    }
    return nullptr;
}

static size_t indexOfMediaDescriptionWithTrackId(const MediaDescriptionVector& mediaDescriptions, const String& trackId)
{
    for (size_t i = 0; i < mediaDescriptions.size(); ++i) {
        if (mediaDescriptions[i]->mediaStreamTrackId() == trackId)
            return i;
    }
    return notFound;
}

static RefPtr<RTCRtpSender> takeFirstSenderOfType(RtpSenderVector& senders, const String& type)
{
    for (unsigned i = 0; i < senders.size(); ++i) {
        if (senders[i]->track()->kind() == type) {
            RefPtr<RTCRtpSender> sender = senders[i];
            senders.remove(i);
            return sender;
        }
    }
    return nullptr;
}

static void updateMediaDescriptionsWithSenders(const MediaDescriptionVector& mediaDescriptions, RtpSenderVector& senders)
{
    // Remove any sender(s) from the senders list that already have their tracks represented by a media
    // description. Mark media descriptions that don't have a sender/track (anymore) as "available".
    for (auto& mdesc : mediaDescriptions) {
        const String& mdescTrackId = mdesc->mediaStreamTrackId();
        size_t index = indexOfSenderWithTrackId(senders, mdescTrackId);
        if (index != notFound)
            senders.remove(index);
        else {
            mdesc->setMediaStreamId(emptyString());
            mdesc->setMediaStreamTrackId(emptyString());
            mdesc->clearSsrcs();
        }
    }

    // Remove any sender(s) from the senders list that can be matched (by track type) to an "available"
    // media description. Mark media descriptions that don't get matched with a sender as receive only.
    for (auto& mdesc : mediaDescriptions) {
        if (mdesc->mediaStreamTrackId() != emptyString())
            continue;

        RefPtr<RTCRtpSender> sender = takeFirstSenderOfType(senders, mdesc->type());
        if (sender) {
            // FIXME: what else needs to be updated to reuse a media description?
            mdesc->setMediaStreamId(sender->mediaStreamIds()[0]);
            mdesc->setMediaStreamTrackId(sender->trackId());
            mdesc->addSsrc(cryptographicallyRandomNumber());
            mdesc->setMode("sendrecv");
        } else
            mdesc->setMode("recvonly");
    }
}

void MediaEndpointPeerConnection::runTask(std::function<void()> task)
{
    if (m_dtlsFingerprint.isNull()) {
        // Only one task needs to be deferred since it will hold off any others until completed.
        ASSERT(!m_initialDeferredTask);
        m_initialDeferredTask = task;
    } else
        callOnMainThread(task);
}

void MediaEndpointPeerConnection::startRunningTasks()
{
    if (!m_initialDeferredTask)
        return;

    m_initialDeferredTask();
    m_initialDeferredTask = nullptr;
}

void MediaEndpointPeerConnection::createOffer(RTCOfferOptions& options, SessionDescriptionPromise&& promise)
{
    const RefPtr<RTCOfferOptions> protectedOptions = &options;
    RefPtr<WrappedSessionDescriptionPromise> wrappedPromise = WrappedSessionDescriptionPromise::create(WTFMove(promise));

    runTask([this, protectedOptions, wrappedPromise]() {
        createOfferTask(*protectedOptions, wrappedPromise->promise());
    });
}

void MediaEndpointPeerConnection::createOfferTask(RTCOfferOptions&, SessionDescriptionPromise& promise)
{
    ASSERT(!m_dtlsFingerprint.isEmpty());

    RefPtr<MediaEndpointSessionConfiguration> configurationSnapshot = internalLocalDescription() ?
        internalLocalDescription()->configuration()->clone() : MediaEndpointSessionConfiguration::create();

    configurationSnapshot->setSessionVersion(m_sdpSessionVersion++);

    RtpTransceiverVector transceivers = RtpTransceiverVector(m_client->getTransceivers());

    // Remove any transceiver objects from transceivers that can be matched to an existing media description.
    for (auto& mediaDescription : configurationSnapshot->mediaDescriptions()) {
        if (mediaDescription->port() == 0) {
            // This media description should be recycled.
            continue;
        }

        RTCRtpTransceiver* transceiver = matchTransceiver(transceivers, [&mediaDescription] (RTCRtpTransceiver& current) {
            return current.mid() == mediaDescription->mid();
        });
        if (!transceiver)
            continue;

        if (mediaDescription->mode() != transceiver->directionalityString())
            mediaDescription->setMode(transceiver->directionalityString());

        transceivers.removeFirst(transceiver);
    }

    // Add media descriptions for remaining transceivers.
    for (auto& transceiver : transceivers) {
        RefPtr<PeerMediaDescription> mediaDescription = PeerMediaDescription::create();
        RTCRtpSender* sender = transceiver->sender();

        mediaDescription->setMode(transceiver->directionalityString());
        mediaDescription->setMid(transceiver->provisionalMid());
        mediaDescription->setMediaStreamId(sender->mediaStreamIds()[0]);
        mediaDescription->setType(sender->trackKind());
        mediaDescription->setPayloads(sender->trackKind() == "audio" ? m_defaultAudioPayloads : m_defaultVideoPayloads);
        mediaDescription->setDtlsFingerprintHashFunction(m_dtlsFingerprintFunction);
        mediaDescription->setDtlsFingerprint(m_dtlsFingerprint);
        mediaDescription->setCname(m_cname);
        mediaDescription->addSsrc(cryptographicallyRandomNumber());
        mediaDescription->setIceUfrag(m_iceUfrag);
        mediaDescription->setIcePassword(m_icePassword);

        if (sender->track()) {
            MediaStreamTrack& track = *sender->track();
            mediaDescription->setMediaStreamTrackId(track.id());
        }

        configurationSnapshot->addMediaDescription(WTFMove(mediaDescription));
    }

    Ref<SessionDescription> description = SessionDescription::create(SessionDescription::Type::Offer, WTFMove(configurationSnapshot));
    promise.resolve(description->toRTCSessionDescription(*m_sdpProcessor));
}

void MediaEndpointPeerConnection::createAnswer(RTCAnswerOptions& options, SessionDescriptionPromise&& promise)
{
    const RefPtr<RTCAnswerOptions> protectedOptions = &options;
    RefPtr<WrappedSessionDescriptionPromise> wrappedPromise = WrappedSessionDescriptionPromise::create(WTFMove(promise));

    runTask([this, protectedOptions, wrappedPromise]() {
        createAnswerTask(*protectedOptions, wrappedPromise->promise());
    });
}

void MediaEndpointPeerConnection::createAnswerTask(RTCAnswerOptions&, SessionDescriptionPromise& promise)
{
    ASSERT(!m_dtlsFingerprint.isEmpty());

    if (!remoteDescription()) {
        // FIXME: Error type?
        promise.reject(DOMError::create("InvalidStateError (no remote description)"));
        return;
    }

    RefPtr<MediaEndpointSessionConfiguration> configurationSnapshot = internalLocalDescription() ?
        internalLocalDescription()->configuration()->clone() : MediaEndpointSessionConfiguration::create();

    configurationSnapshot->setSessionVersion(m_sdpSessionVersion++);

    const MediaDescriptionVector& remoteMediaDescriptions = internalRemoteDescription()->configuration()->mediaDescriptions();
    for (unsigned i = 0; i < remoteMediaDescriptions.size(); ++i) {
        PeerMediaDescription& remoteMediaDescription = *remoteMediaDescriptions[i];

        if (i >= configurationSnapshot->mediaDescriptions().size()) {
            RefPtr<PeerMediaDescription> newMediaDescription = PeerMediaDescription::create();

            newMediaDescription->setType(remoteMediaDescription.type());
            newMediaDescription->setMid(remoteMediaDescription.mid());
            newMediaDescription->setDtlsSetup(remoteMediaDescription.dtlsSetup() == "active" ? "passive" : "active");
            newMediaDescription->setDtlsFingerprintHashFunction(m_dtlsFingerprintFunction);
            newMediaDescription->setDtlsFingerprint(m_dtlsFingerprint);
            newMediaDescription->setCname(m_cname);
            newMediaDescription->setIceUfrag(m_iceUfrag);
            newMediaDescription->setIcePassword(m_icePassword);

            configurationSnapshot->addMediaDescription(WTFMove(newMediaDescription));
        }

        PeerMediaDescription& localMediaDescription = *configurationSnapshot->mediaDescriptions()[i];

        localMediaDescription.setPayloads(remoteMediaDescription.payloads());
        localMediaDescription.setRtcpMux(remoteMediaDescription.rtcpMux());

        if (!localMediaDescription.ssrcs().size())
            localMediaDescription.addSsrc(cryptographicallyRandomNumber());

        if (localMediaDescription.dtlsSetup() == "actpass")
            localMediaDescription.setDtlsSetup("passive");
    }

    RtpSenderVector senders = m_client->getSenders();
    updateMediaDescriptionsWithSenders(configurationSnapshot->mediaDescriptions(), senders);

    Ref<SessionDescription> description = SessionDescription::create(SessionDescription::Type::Answer, WTFMove(configurationSnapshot));
    promise.resolve(description->toRTCSessionDescription(*m_sdpProcessor));
}

void MediaEndpointPeerConnection::setLocalDescription(RTCSessionDescription& description, VoidPromise&& promise)
{
    RefPtr<RTCSessionDescription> protectedDescription = &description;
    RefPtr<WrappedVoidPromise> wrappedPromise = WrappedVoidPromise::create(WTFMove(promise));

    runTask([this, protectedDescription, wrappedPromise]() {
        setLocalDescriptionTask(*protectedDescription, wrappedPromise->promise());
    });
}

static void updateSendSources(const MediaDescriptionVector& localMediaDescriptions, const MediaDescriptionVector& remoteMediaDescriptions, const RtpSenderVector& senders)
{
    for (unsigned i = 0; i < remoteMediaDescriptions.size(); ++i) {
        if (remoteMediaDescriptions[i]->type() != "audio" && remoteMediaDescriptions[i]->type() != "video")
            continue;

        if (localMediaDescriptions.size() <= i)
            return;

        size_t index = indexOfSenderWithTrackId(senders, localMediaDescriptions[i]->mediaStreamTrackId());
        if (index != notFound)
            remoteMediaDescriptions[i]->setSource(&senders[index]->track()->source());
    }
}

static bool allSendersRepresented(const RtpSenderVector& senders, const MediaDescriptionVector& mediaDescriptions)
{
    for (auto& sender : senders) {
        if (indexOfMediaDescriptionWithTrackId(mediaDescriptions, sender->trackId()) == notFound)
            return false;
    }
    return true;
}

void MediaEndpointPeerConnection::setLocalDescriptionTask(RTCSessionDescription& description, VoidPromise& promise)
{
    RefPtr<DOMError> error;
    RefPtr<SessionDescription> newDescription = SessionDescription::create(description, *m_sdpProcessor, error);
    if (!newDescription) {
        promise.reject(error);
        return;
    }

    if (!localDescriptionTypeValidForState(newDescription->type())) {
        promise.reject(DOMError::create("InvalidSessionDescriptionError (bad description type for current state)"));
        return;
    }

    const MediaDescriptionVector& mediaDescriptions = newDescription->configuration()->mediaDescriptions();
    unsigned previousNumberOfMediaDescriptions = internalLocalDescription() ?
        internalLocalDescription()->configuration()->mediaDescriptions().size() : 0;
    bool hasNewMediaDescriptions = mediaDescriptions.size() > previousNumberOfMediaDescriptions;
    bool isInitiator = newDescription->type() == SessionDescription::Type::Offer;

    if (hasNewMediaDescriptions) {
        MediaEndpoint::UpdateResult result = m_mediaEndpoint->updateReceiveConfiguration(newDescription->configuration(), isInitiator);

        if (result == MediaEndpoint::UpdateResult::SuccessWithIceRestart) {
            if (m_client->internalIceGatheringState() != IceGatheringState::Gathering)
                m_client->updateIceGatheringState(IceGatheringState::Gathering);

            if (m_client->internalIceConnectionState() != IceConnectionState::Completed)
                m_client->updateIceConnectionState(IceConnectionState::Connected);

            printf("ICE restart not implemented\n");

        } else if (result == MediaEndpoint::UpdateResult::Failed) {
            // FIXME: Error type?
            promise.reject(DOMError::create("IncompatibleSessionDescriptionError (receive configuration)"));
            return;
        }

        // Associate media descriptions with transceivers (set provisional mid to 'final' mid).
        const RtpTransceiverVector& transceivers = m_client->getTransceivers();
        for (unsigned i = previousNumberOfMediaDescriptions; i < mediaDescriptions.size(); ++i) {
            PeerMediaDescription& mediaDescription = *mediaDescriptions[i];

            RTCRtpTransceiver* transceiver = matchTransceiver(transceivers, [&mediaDescription] (RTCRtpTransceiver& current) {
                return current.provisionalMid() == mediaDescription.mid();
            });
            if (transceiver)
                transceiver->setMid(transceiver->provisionalMid());
        }
    }

    if (internalRemoteDescription()) {
        updateSendSources(mediaDescriptions, internalRemoteDescription()->configuration()->mediaDescriptions(), m_client->getSenders());

        if (m_mediaEndpoint->updateSendConfiguration(internalRemoteDescription()->configuration(), isInitiator) == MediaEndpoint::UpdateResult::Failed) {
            // FIXME: Error type?
            promise.reject(DOMError::create("IncompatibleSessionDescriptionError (send configuration)"));
            return;
        }
    }

    if (allSendersRepresented(m_client->getSenders(), mediaDescriptions))
        clearNegotiationNeededState();

    SignalingState newSignalingState;

    // Update state and local descriptions according to setLocal/RemoteDescription processing model
    switch (newDescription->type()) {
    case SessionDescription::Type::Offer:
        if (newDescription->isLaterThan(m_currentLocalDescription.get())) {
            m_pendingLocalDescription = newDescription;
            newSignalingState = SignalingState::HaveLocalOffer;
        }

        break;

    case SessionDescription::Type::Answer:
        m_currentLocalDescription = newDescription;
        m_currentRemoteDescription = m_pendingRemoteDescription;
        m_pendingLocalDescription = nullptr;
        m_pendingRemoteDescription = nullptr;
        newSignalingState = SignalingState::Stable;
        break;

    case SessionDescription::Type::Rollback:
        // FIXME: rollback is not supported in the platform yet
        m_pendingLocalDescription = nullptr;
        newSignalingState = SignalingState::Stable;
        break;

    case SessionDescription::Type::Pranswer:
        m_pendingLocalDescription = newDescription;
        newSignalingState = SignalingState::HaveLocalPrAnswer;
        break;
    }

    if (newSignalingState != m_client->internalSignalingState()) {
        m_client->setSignalingState(newSignalingState);
        m_client->fireEvent(Event::create(eventNames().signalingstatechangeEvent, false, false));
    }

    // FIXME: do this even if an ice start was done?
    if (m_client->internalIceGatheringState() == IceGatheringState::New && mediaDescriptions.size())
        m_client->updateIceGatheringState(IceGatheringState::Gathering);

    if (m_client->internalSignalingState() == SignalingState::Stable && m_negotiationNeeded)
        m_client->scheduleNegotiationNeededEvent();

    promise.resolve(nullptr);
}

RefPtr<RTCSessionDescription> MediaEndpointPeerConnection::localDescription() const
{
    return createRTCSessionDescription(internalLocalDescription());
}

RefPtr<RTCSessionDescription> MediaEndpointPeerConnection::currentLocalDescription() const
{
    return createRTCSessionDescription(m_currentLocalDescription.get());
}

RefPtr<RTCSessionDescription> MediaEndpointPeerConnection::pendingLocalDescription() const
{
    return createRTCSessionDescription(m_pendingLocalDescription.get());
}

static Vector<RefPtr<MediaPayload>> filterPayloads(const Vector<RefPtr<MediaPayload>>& remotePayloads, const Vector<RefPtr<MediaPayload>>& defaultPayloads)
{
    Vector<RefPtr<MediaPayload>> filteredPayloads;

    for (auto& remotePayload : remotePayloads) {
        MediaPayload* defaultPayload = nullptr;
        for (auto& p : defaultPayloads) {
            if (p->encodingName() == remotePayload->encodingName().convertToASCIIUppercase()) {
                defaultPayload = p.get();
                break;
            }
        }
        if (!defaultPayload)
            continue;

        if (defaultPayload->parameters().contains("packetizationMode") && remotePayload->parameters().contains("packetizationMode")
            && (defaultPayload->parameters().get("packetizationMode") != defaultPayload->parameters().get("packetizationMode")))
            continue;

        filteredPayloads.append(remotePayload);
    }

    return filteredPayloads;
}

void MediaEndpointPeerConnection::setRemoteDescription(RTCSessionDescription& description, VoidPromise&& promise)
{
    RefPtr<RTCSessionDescription> protectedDescription = &description;
    RefPtr<WrappedVoidPromise> wrappedPromise = WrappedVoidPromise::create(WTFMove(promise));

    runTask([this, protectedDescription, wrappedPromise]() {
        setRemoteDescriptionTask(*protectedDescription, wrappedPromise->promise());
    });
}

void MediaEndpointPeerConnection::setRemoteDescriptionTask(RTCSessionDescription& description, VoidPromise& promise)
{
    RefPtr<DOMError> error;
    RefPtr<SessionDescription> newDescription = SessionDescription::create(description, *m_sdpProcessor, error);
    if (!newDescription) {
        promise.reject(error);
        return;
    }

    if (!remoteDescriptionTypeValidForState(newDescription->type())) {
        promise.reject(DOMError::create("InvalidSessionDescriptionError (bad description type for current state)"));
        return;
    }

    const MediaDescriptionVector& mediaDescriptions = newDescription->configuration()->mediaDescriptions();
    RtpSenderVector senders = m_client->getSenders();

    for (auto& mediaDescription : mediaDescriptions) {
        if (mediaDescription->type() != "audio" && mediaDescription->type() != "video")
            continue;

        mediaDescription->setPayloads(filterPayloads(mediaDescription->payloads(),
            mediaDescription->type() == "audio" ? m_defaultAudioPayloads : m_defaultVideoPayloads));
    }

    bool isInitiator = newDescription->type() == SessionDescription::Type::Answer;

    if (internalLocalDescription())
        updateSendSources(internalLocalDescription()->configuration()->mediaDescriptions(), mediaDescriptions, senders);

    if (m_mediaEndpoint->updateSendConfiguration(newDescription->configuration(), isInitiator) == MediaEndpoint::UpdateResult::Failed) {
        // FIXME: Error type?
        promise.reject(DOMError::create("IncompatibleSessionDescriptionError (send configuration)"));
        return;
    }

    SignalingState newSignalingState;

    // Update state and local descriptions according to setLocal/RemoteDescription processing model
    switch (newDescription->type()) {
    case SessionDescription::Type::Offer:
        if (newDescription->isLaterThan(m_currentRemoteDescription.get())) {
            m_pendingRemoteDescription = newDescription;
            newSignalingState = SignalingState::HaveRemoteOffer;
        }
        break;

    case SessionDescription::Type::Answer:
        m_currentRemoteDescription = newDescription;
        m_currentLocalDescription = m_pendingLocalDescription;
        m_pendingRemoteDescription = nullptr;
        m_pendingLocalDescription = nullptr;
        newSignalingState = SignalingState::Stable;
        break;

    case SessionDescription::Type::Rollback:
        // FIXME: rollback is not supported in the platform yet
        m_pendingRemoteDescription = nullptr;
        newSignalingState = SignalingState::Stable;
        break;

    case SessionDescription::Type::Pranswer:
        m_pendingRemoteDescription = newDescription;
        newSignalingState = SignalingState::HaveRemotePrAnswer;
        break;
    }

    if (newSignalingState != m_client->internalSignalingState()) {
        m_client->setSignalingState(newSignalingState);
        m_client->fireEvent(Event::create(eventNames().signalingstatechangeEvent, false, false));
    }

    promise.resolve(nullptr);
}

RefPtr<RTCSessionDescription> MediaEndpointPeerConnection::remoteDescription() const
{
    return createRTCSessionDescription(internalRemoteDescription());
}

RefPtr<RTCSessionDescription> MediaEndpointPeerConnection::currentRemoteDescription() const
{
    return createRTCSessionDescription(m_currentRemoteDescription.get());
}

RefPtr<RTCSessionDescription> MediaEndpointPeerConnection::pendingRemoteDescription() const
{
    return createRTCSessionDescription(m_pendingRemoteDescription.get());
}

void MediaEndpointPeerConnection::setConfiguration(RTCConfiguration& configuration)
{
    m_mediaEndpoint->setConfiguration(createMediaEndpointConfiguration(configuration));
}

void MediaEndpointPeerConnection::addIceCandidate(RTCIceCandidate& rtcCandidate, PeerConnection::VoidPromise&& promise)
{
    RefPtr<RTCIceCandidate> protectedCandidate = &rtcCandidate;
    RefPtr<WrappedVoidPromise> wrappedPromise = WrappedVoidPromise::create(WTFMove(promise));

    runTask([this, protectedCandidate, wrappedPromise]() {
        addIceCandidateTask(*protectedCandidate, wrappedPromise->promise());
    });
}

void MediaEndpointPeerConnection::addIceCandidateTask(RTCIceCandidate& rtcCandidate, PeerConnection::VoidPromise& promise)
{
    if (!remoteDescription()) {
        // FIXME: Error type?
        promise.reject(DOMError::create("InvalidStateError (no remote description)"));
        return;
    }

    RefPtr<IceCandidate> candidate;
    SDPProcessor::Result result = m_sdpProcessor->parseCandidateLine(rtcCandidate.candidate(), candidate);
    if (result != SDPProcessor::Result::Success) {
        if (result == SDPProcessor::Result::ParseError)
            promise.reject(DOMError::create("SyntaxError (malformed candidate)"));
        else
            LOG_ERROR("SDPProcessor internal error");
        return;
    }

    const MediaDescriptionVector& remoteMediaDescriptions = internalRemoteDescription()->configuration()->mediaDescriptions();
    // Optional<unsigned short> sdpMLineIndex = ;
    unsigned mdescIndex = rtcCandidate.sdpMLineIndex().valueOr(0);

    if (mdescIndex >= remoteMediaDescriptions.size()) {
        // FIXME: Error type?
        promise.reject(DOMError::create("InvalidSdpMlineIndex (sdpMLineIndex out of range"));
        return;
    }

    PeerMediaDescription& mdesc = *remoteMediaDescriptions[mdescIndex];
    mdesc.addIceCandidate(candidate.copyRef());

    m_mediaEndpoint->addRemoteCandidate(*candidate, mdescIndex, mdesc.iceUfrag(), mdesc.icePassword());

    promise.resolve(nullptr);
}

void MediaEndpointPeerConnection::getStats(MediaStreamTrack*, PeerConnection::StatsPromise&& promise)
{
    promise.reject(DOMError::create("Not implemented"));
}

void MediaEndpointPeerConnection::replaceTrack(RTCRtpSender& sender, RefPtr<MediaStreamTrack>&& withTrack, PeerConnection::VoidPromise&& promise)
{
    size_t mdescIndex = notFound;
    SessionDescription* localDescription = internalLocalDescription();

    if (localDescription)
        mdescIndex = indexOfMediaDescriptionWithTrackId(localDescription->configuration()->mediaDescriptions(), sender.trackId());

    if (mdescIndex == notFound) {
        sender.setTrack(WTFMove(withTrack));
        promise.resolve(nullptr);
        return;
    }

    RefPtr<RTCRtpSender> protectedSender = &sender;
    RefPtr<MediaStreamTrack> protectedTrack = withTrack;
    RefPtr<WrappedVoidPromise> wrappedPromise = WrappedVoidPromise::create(WTFMove(promise));

    runTask([this, protectedSender, mdescIndex, protectedTrack, wrappedPromise]() mutable {
        replaceTrackTask(*protectedSender, mdescIndex, WTFMove(protectedTrack), wrappedPromise->promise());
    });
}

void MediaEndpointPeerConnection::replaceTrackTask(RTCRtpSender& sender, size_t mdescIndex, RefPtr<MediaStreamTrack>&& withTrack, PeerConnection::VoidPromise& promise)
{
    m_mediaEndpoint->replaceSendSource(withTrack->source(), mdescIndex);

    sender.setTrack(WTFMove(withTrack));
    promise.resolve(nullptr);
}

void MediaEndpointPeerConnection::stop()
{
    m_mediaEndpoint->stop();
}

void MediaEndpointPeerConnection::markAsNeedingNegotiation()
{
    if (m_negotiationNeeded)
        return;

    m_negotiationNeeded = true;

    if (m_client->internalSignalingState() == SignalingState::Stable)
        m_client->scheduleNegotiationNeededEvent();
}

bool MediaEndpointPeerConnection::localDescriptionTypeValidForState(SessionDescription::Type type) const
{
    switch (m_client->internalSignalingState()) {
    case SignalingState::Stable:
        return type == SessionDescription::Type::Offer;
    case SignalingState::HaveLocalOffer:
        return type == SessionDescription::Type::Offer;
    case SignalingState::HaveRemoteOffer:
        return type == SessionDescription::Type::Answer || type == SessionDescription::Type::Pranswer;
    case SignalingState::HaveLocalPrAnswer:
        return type == SessionDescription::Type::Answer || type == SessionDescription::Type::Pranswer;
    default:
        return false;
    };

    ASSERT_NOT_REACHED();
    return false;
}

bool MediaEndpointPeerConnection::remoteDescriptionTypeValidForState(SessionDescription::Type type) const
{
    switch (m_client->internalSignalingState()) {
    case SignalingState::Stable:
        return type == SessionDescription::Type::Offer;
    case SignalingState::HaveLocalOffer:
        return type == SessionDescription::Type::Answer || type == SessionDescription::Type::Pranswer;
    case SignalingState::HaveRemoteOffer:
        return type == SessionDescription::Type::Offer;
    case SignalingState::HaveRemotePrAnswer:
        return type == SessionDescription::Type::Answer || type == SessionDescription::Type::Pranswer;
    default:
        return false;
    };

    ASSERT_NOT_REACHED();
    return false;
}

SessionDescription* MediaEndpointPeerConnection::internalLocalDescription() const
{
    return m_pendingLocalDescription ? m_pendingLocalDescription.get() : m_currentLocalDescription.get();
}

SessionDescription* MediaEndpointPeerConnection::internalRemoteDescription() const
{
    return m_pendingRemoteDescription ? m_pendingRemoteDescription.get() : m_currentRemoteDescription.get();
}

RefPtr<RTCSessionDescription> MediaEndpointPeerConnection::createRTCSessionDescription(SessionDescription* description) const
{
    return description ? description->toRTCSessionDescription(*m_sdpProcessor) : nullptr;
}

void MediaEndpointPeerConnection::gotDtlsFingerprint(const String& fingerprint, const String& fingerprintFunction)
{
    ASSERT(isMainThread());

    m_dtlsFingerprint = fingerprint;
    m_dtlsFingerprintFunction = fingerprintFunction;

    startRunningTasks();
}

void MediaEndpointPeerConnection::gotIceCandidate(unsigned mdescIndex, RefPtr<IceCandidate>&& candidate)
{
    ASSERT(isMainThread());

    PeerMediaDescription& mdesc = *internalLocalDescription()->configuration()->mediaDescriptions()[mdescIndex];
    mdesc.addIceCandidate(candidate.copyRef());

    // FIXME: should we still do this?
    if (!candidate->address().contains(':')) {
        // Not IPv6
        if (candidate->componentId() == 1) {
            // RTP
            if (mdesc.address().isEmpty() || mdesc.address() == "0.0.0.0") {
                mdesc.setAddress(candidate->address());
                mdesc.setPort(candidate->port());
            }
        } else {
            // RTCP
            if (mdesc.rtcpAddress().isEmpty() || !mdesc.rtcpPort()) {
                mdesc.setRtcpAddress(candidate->address());
                mdesc.setRtcpPort(candidate->port());
            }
        }
    }

    String candidateLine;
    SDPProcessor::Result result = m_sdpProcessor->generateCandidateLine(*candidate, candidateLine);
    if (result != SDPProcessor::Result::Success) {
        LOG_ERROR("SDPProcessor internal error");
        return;
    }

    RefPtr<RTCIceCandidate> iceCandidate = RTCIceCandidate::create(candidateLine, "", mdescIndex);

    m_client->fireEvent(RTCIceCandidateEvent::create(false, false, WTFMove(iceCandidate)));
}

void MediaEndpointPeerConnection::doneGatheringCandidates(unsigned)
{
    ASSERT(isMainThread());

    // FIXME: Fire event when all media descriptions that are currently gathering have recevied
    // this signal. Old broken implementation was removed.
}

void MediaEndpointPeerConnection::gotRemoteSource(unsigned mdescIndex, RefPtr<RealtimeMediaSource>&& source)
{
    ASSERT(isMainThread());

    if (m_client->internalSignalingState() == SignalingState::Closed)
        return;

    const MediaDescriptionVector& remoteMediaDescriptions = internalRemoteDescription()->configuration()->mediaDescriptions();

    if (mdescIndex >= remoteMediaDescriptions.size()) {
        printf("Warning: No remote configuration for incoming source.\n");
        return;
    }

    PeerMediaDescription& mediaDescription = *remoteMediaDescriptions[mdescIndex];
    String trackId = mediaDescription.mediaStreamTrackId();

    if (trackId.isEmpty()) {
        // Non WebRTC media description (e.g. legacy)
        trackId = createCanonicalUUIDString();
    }

    // FIXME: track should be set to muted (not supported yet)
    // FIXME: MediaStream handling not implemented

    RefPtr<MediaStreamTrackPrivate> trackPrivate = MediaStreamTrackPrivate::create(WTFMove(source), trackId);
    RefPtr<MediaStreamTrack> track = MediaStreamTrack::create(*m_client->scriptExecutionContext(), *trackPrivate);
    RefPtr<RTCRtpReceiver> receiver = RTCRtpReceiver::create(track.copyRef());

    m_client->addReceiver(*receiver);
    m_client->fireEvent(RTCTrackEvent::create(eventNames().trackEvent, false, false, WTFMove(receiver), WTFMove(track)));
}

} // namespace WebCore

#endif // ENABLE(WEB_RTC)
