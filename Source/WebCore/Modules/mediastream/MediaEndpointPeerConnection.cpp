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

#if ENABLE(MEDIA_STREAM)
#include "MediaEndpointPeerConnection.h"

#include "CryptoDigest.h"
#include "DOMError.h"
#include "JSDOMError.h"
#include "JSRTCSessionDescription.h"
#include "MediaEndpointConfigurationConversions.h"
#include "MediaStreamTrack.h"
#include "PeerConnectionBackend.h"
#include "PeerMediaDescription.h"
#include "RTCConfiguration.h"
#include "RTCIceCandidate.h"
#include "RTCIceCandidateEvent.h"
#include "RTCOfferAnswerOptions.h"
#include "RTCSessionDescription.h"
#include "RTCRtpReceiver.h"
#include "RTCRtpSender.h"
#include "RTCTrackEvent.h"
#include "UUID.h"
#include <wtf/text/Base64.h>

// FIXME: Headers for sdp.js supported SDP conversion is kept on the side for now
#include "Document.h"
#include "Frame.h"
#include "JSDOMWindow.h"
#include "ScriptController.h"
#include "ScriptGlobalObject.h"
#include "ScriptSourceCode.h"
#include "SDPScriptResource.h"
#include <bindings/ScriptObject.h>

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
        return *adoptRef(new WrappedSessionDescriptionPromise(WTF::move(promise)));
    }

    SessionDescriptionPromise& promise() { return m_promise; }

private:
    WrappedSessionDescriptionPromise(SessionDescriptionPromise&& promise)
        : m_promise(WTF::move(promise))
    { }

    SessionDescriptionPromise m_promise;
};

class WrappedVoidPromise : public RefCounted<WrappedVoidPromise> {
public:
    static Ref<WrappedVoidPromise> create(VoidPromise&& promise)
    {
        return *adoptRef(new WrappedVoidPromise(WTF::move(promise)));
    }

    VoidPromise& promise() { return m_promise; }

private:
    WrappedVoidPromise(VoidPromise&& promise)
        : m_promise(WTF::move(promise))
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

static RefPtr<MediaEndpointInit> createMediaEndpointInit(RTCConfiguration& rtcConfig)
{
    Vector<RefPtr<IceServerInfo>> iceServers;
    for (auto& server : rtcConfig.iceServers())
        iceServers.append(IceServerInfo::create(server->urls(), server->credential(), server->username()));

    return MediaEndpointInit::create(iceServers, rtcConfig.iceTransportPolicy(), rtcConfig.bundlePolicy());
}

MediaEndpointPeerConnection::MediaEndpointPeerConnection(PeerConnectionBackendClient* client)
    : m_client(client)
    , m_cname(randomString(16))
    , m_iceUfrag(randomString(4))
    , m_icePassword(randomString(22))
    , m_sdpSessionVersion(0)
    , m_negotiationNeeded(false)
{
    m_mediaEndpoint = MediaEndpoint::create(this);
    ASSERT(m_mediaEndpoint);

    enqueueOperation([this]() {
        m_mediaEndpoint->getDtlsCertificate();
    });

    m_defaultAudioPayloads = m_mediaEndpoint->getDefaultAudioPayloads();
    m_defaultVideoPayloads = m_mediaEndpoint->getDefaultVideoPayloads();
}

MediaEndpointPeerConnection::~MediaEndpointPeerConnection()
{
}

static size_t indexOfSenderWithTrackId(const RtpSenderVector& senders, const String& trackId)
{
    for (size_t i = 0; i < senders.size(); ++i) {
        if (senders[i]->track()->id() == trackId)
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
            mdesc->setMediaStreamId(sender->mediaStreamId());
            mdesc->setMediaStreamTrackId(sender->track()->id());
            mdesc->addSsrc(cryptographicallyRandomNumber());
            mdesc->setMode("sendrecv");
        } else
            mdesc->setMode("recvonly");
    }
}

void MediaEndpointPeerConnection::enqueueOperation(std::function<void ()> operation)
{
    m_operationsQueue.append(operation);
    if (m_operationsQueue.size() == 1)
        callOnMainThread(m_operationsQueue[0]);
}

void MediaEndpointPeerConnection::completeQueuedOperation()
{
    ASSERT( m_operationsQueue.size());
    m_operationsQueue.remove(0);
    if (!m_operationsQueue.isEmpty())
        callOnMainThread(m_operationsQueue[0]);
}

void MediaEndpointPeerConnection::createOffer(const RefPtr<RTCOfferOptions>& options, SessionDescriptionPromise&& promise)
{
    const RefPtr<RTCOfferOptions> protectedOptions = options;
    RefPtr<WrappedSessionDescriptionPromise> wrappedPromise = WrappedSessionDescriptionPromise::create(WTF::move(promise));

    enqueueOperation([this, protectedOptions, wrappedPromise]() {
        queuedCreateOffer(protectedOptions, wrappedPromise->promise());
    });
}

void MediaEndpointPeerConnection::queuedCreateOffer(const RefPtr<RTCOfferOptions>& options, SessionDescriptionPromise& promise)
{
    ASSERT(!m_dtlsFingerprint.isEmpty());

    RefPtr<MediaEndpointConfiguration> configurationSnapshot = internalLocalDescription() ?
        internalLocalDescription()->configuration()->clone() : MediaEndpointConfiguration::create();

    configurationSnapshot->setSessionVersion(m_sdpSessionVersion++);

    RtpSenderVector senders = m_client->getSenders();
    updateMediaDescriptionsWithSenders(configurationSnapshot->mediaDescriptions(), senders);

    // Add media descriptions for remaining senders.
    for (auto& sender : senders) {
        RefPtr<PeerMediaDescription> mediaDescription = PeerMediaDescription::create();
        MediaStreamTrack* track = sender->track();

        mediaDescription->setMediaStreamId(sender->mediaStreamId());
        mediaDescription->setMediaStreamTrackId(track->id());
        mediaDescription->setType(track->kind());
        mediaDescription->setPort(9);
        mediaDescription->setAddress("0.0.0.0");
        mediaDescription->setMode("sendrecv");
        mediaDescription->setPayloads(track->kind() == "audio" ? m_defaultAudioPayloads : m_defaultVideoPayloads);
        mediaDescription->setRtcpMux(true);
        mediaDescription->setDtlsSetup("actpass");
        mediaDescription->setDtlsFingerprintHashFunction("sha-256");
        mediaDescription->setDtlsFingerprint(m_dtlsFingerprint);
        mediaDescription->setCname(m_cname);
        mediaDescription->addSsrc(cryptographicallyRandomNumber());
        mediaDescription->setIceUfrag(m_iceUfrag);
        mediaDescription->setIcePassword(m_icePassword);

        configurationSnapshot->addMediaDescription(WTF::move(mediaDescription));
    }

    int extraMediaDescriptionCount = options->offerToReceiveAudio() + options->offerToReceiveVideo();
    for (int i = 0; i < extraMediaDescriptionCount; ++i) {
        String type = i < options->offerToReceiveAudio() ? "audio" : "video";
        RefPtr<PeerMediaDescription> mediaDescription = PeerMediaDescription::create();

        mediaDescription->setType(type);
        mediaDescription->setPort(9);
        mediaDescription->setAddress("0.0.0.0");
        mediaDescription->setMode("recvonly");
        mediaDescription->setPayloads(type == "audio" ? m_defaultAudioPayloads : m_defaultVideoPayloads);
        mediaDescription->setRtcpMux(true);
        mediaDescription->setDtlsSetup("actpass");
        mediaDescription->setDtlsFingerprintHashFunction("sha-256");
        mediaDescription->setDtlsFingerprint(m_dtlsFingerprint);
        mediaDescription->setIceUfrag(m_iceUfrag);
        mediaDescription->setIcePassword(m_icePassword);

        configurationSnapshot->addMediaDescription(WTF::move(mediaDescription));
    }

    String json = MediaEndpointConfigurationConversions::toJSON(configurationSnapshot.get());
    RefPtr<RTCSessionDescription> offer = RTCSessionDescription::create("offer", toSDP(json));

    promise.resolve(offer);
    completeQueuedOperation();
}

void MediaEndpointPeerConnection::createAnswer(const RefPtr<RTCAnswerOptions>& options, SessionDescriptionPromise&& promise)
{
    const RefPtr<RTCAnswerOptions> protectedOptions = options;
    RefPtr<WrappedSessionDescriptionPromise> wrappedPromise = WrappedSessionDescriptionPromise::create(WTF::move(promise));

    enqueueOperation([this, protectedOptions, wrappedPromise]() mutable {
        queuedCreateAnswer(protectedOptions, wrappedPromise->promise());
    });
}

void MediaEndpointPeerConnection::queuedCreateAnswer(const RefPtr<RTCAnswerOptions>&, SessionDescriptionPromise& promise)
{
    ASSERT(!m_dtlsFingerprint.isEmpty());

    RefPtr<MediaEndpointConfiguration> configurationSnapshot = internalLocalDescription() ?
        internalLocalDescription()->configuration()->clone() : MediaEndpointConfiguration::create();

    configurationSnapshot->setSessionVersion(m_sdpSessionVersion++);

    const MediaDescriptionVector& remoteMediaDescriptions = internalRemoteDescription()->configuration()->mediaDescriptions();
    for (unsigned i = 0; i < remoteMediaDescriptions.size(); ++i) {
        RefPtr<PeerMediaDescription> remoteMediaDescription = remoteMediaDescriptions[i];
        RefPtr<PeerMediaDescription> localMediaDescription;

        if (i < configurationSnapshot->mediaDescriptions().size())
            localMediaDescription = configurationSnapshot->mediaDescriptions()[i];
        else {
            localMediaDescription = PeerMediaDescription::create();
            localMediaDescription->setType(remoteMediaDescription->type());
            localMediaDescription->setPort(9);
            localMediaDescription->setAddress("0.0.0.0");
            localMediaDescription->setDtlsSetup(remoteMediaDescription->dtlsSetup() == "active" ? "passive" : "active");
            localMediaDescription->setDtlsFingerprintHashFunction("sha-256");
            localMediaDescription->setDtlsFingerprint(m_dtlsFingerprint);
            localMediaDescription->setCname(m_cname);
            localMediaDescription->setIceUfrag(m_iceUfrag);
            localMediaDescription->setIcePassword(m_icePassword);

            configurationSnapshot->addMediaDescription(localMediaDescription.copyRef());
        }

        localMediaDescription->setPayloads(remoteMediaDescription->payloads());

        localMediaDescription->setRtcpMux(remoteMediaDescription->rtcpMux());

        if (!localMediaDescription->ssrcs().size())
            localMediaDescription->addSsrc(cryptographicallyRandomNumber());

        if (localMediaDescription->dtlsSetup() == "actpass")
            localMediaDescription->setDtlsSetup("passive");
    }

    RtpSenderVector senders = m_client->getSenders();
    updateMediaDescriptionsWithSenders(configurationSnapshot->mediaDescriptions(), senders);

    String json = MediaEndpointConfigurationConversions::toJSON(configurationSnapshot.get());
    RefPtr<RTCSessionDescription> answer = RTCSessionDescription::create("answer", toSDP(json));

    promise.resolve(answer);
    completeQueuedOperation();
}

void MediaEndpointPeerConnection::setLocalDescription(RTCSessionDescription* description, VoidPromise&& promise)
{
    RefPtr<RTCSessionDescription> protectedDescription = description;
    RefPtr<WrappedVoidPromise> wrappedPromise = WrappedVoidPromise::create(WTF::move(promise));

    enqueueOperation([this, protectedDescription, wrappedPromise]() {
        queuedSetLocalDescription(protectedDescription.get(), wrappedPromise->promise());
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
            remoteMediaDescriptions[i]->setSource(senders[index]->track()->source());
    }
}

static bool allSendersRepresented(const RtpSenderVector& senders, const MediaDescriptionVector& mediaDescriptions)
{
    for (auto& sender : senders) {
        bool senderIsRepresented = false;
        for (auto& mdesc : mediaDescriptions) {
            if (mdesc->mediaStreamTrackId() == sender->track()->id()) {
                senderIsRepresented = true;
                break;
            }
        }
        if (!senderIsRepresented)
            return false;
    }
    return true;
}

void MediaEndpointPeerConnection::queuedSetLocalDescription(RTCSessionDescription* description, VoidPromise& promise)
{
    if (m_client->internalSignalingState() == SignalingState::Closed)
        return;

    SessionDescription::Type descriptionType = parseDescriptionType(description->type());

    if (!localDescriptionTypeValidForState(descriptionType)) {
        // FIXME: Error type?
        promise.reject(DOMError::create("InvalidSessionDescriptionError (bad description type for current state)"));
        completeQueuedOperation();
        return;
    }

    String json = fromSDP(description->sdp());
    RefPtr<MediaEndpointConfiguration> parsedConfiguration = MediaEndpointConfigurationConversions::fromJSON(json);

    if (!parsedConfiguration) {
        // FIXME: Error type?
        promise.reject(DOMError::create("InvalidSessionDescriptionError (unable to parse description)"));
        completeQueuedOperation();
        return;
    }

    unsigned previousNumberOfMediaDescriptions = internalLocalDescription() ? internalLocalDescription()->configuration()->mediaDescriptions().size() : 0;
    unsigned numberOfMediaDescriptions = parsedConfiguration->mediaDescriptions().size();
    bool hasNewMediaDescriptions = numberOfMediaDescriptions > previousNumberOfMediaDescriptions;
    bool isInitiator = descriptionType == SessionDescription::Type::Offer;

    if (hasNewMediaDescriptions) {
        MediaEndpointPrepareResult prepareResult = m_mediaEndpoint->prepareToReceive(parsedConfiguration.get(), isInitiator);

        if (prepareResult == MediaEndpointPrepareResult::SuccessWithIceRestart) {
            if (m_client->internalIceGatheringState() != IceGatheringState::Gathering)
                m_client->updateIceGatheringState(IceGatheringState::Gathering);

            if (m_client->internalIceConnectionState() != IceConnectionState::Completed)
                m_client->updateIceConnectionState(IceConnectionState::Connected);

            printf("ICE restart not implemented\n");

        } else if (prepareResult == MediaEndpointPrepareResult::Failed) {
            // FIXME: Error type?
            promise.reject(DOMError::create("IncompatibleSessionDescriptionError (receive configuration)"));
            completeQueuedOperation();
            return;
        }
    }

    if (internalRemoteDescription()) {
        updateSendSources(parsedConfiguration->mediaDescriptions(), internalRemoteDescription()->configuration()->mediaDescriptions(), m_client->getSenders());

        if (m_mediaEndpoint->prepareToSend(internalRemoteDescription()->configuration(), isInitiator) == MediaEndpointPrepareResult::Failed) {
            // FIXME: Error type?
            promise.reject(DOMError::create("IncompatibleSessionDescriptionError (send configuration)"));
            completeQueuedOperation();
            return;
        }
    }

    if (allSendersRepresented(m_client->getSenders(), parsedConfiguration->mediaDescriptions()))
        clearNegotiationNeededState();

    RefPtr<SessionDescription> newDescription = SessionDescription::create(descriptionType, WTF::move(parsedConfiguration));
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
    if (m_client->internalIceGatheringState() == IceGatheringState::New && numberOfMediaDescriptions)
        m_client->updateIceGatheringState(IceGatheringState::Gathering);

    if (m_client->internalSignalingState() == SignalingState::Stable && m_negotiationNeeded)
        m_client->scheduleNegotiationNeededEvent();

    promise.resolve(nullptr);
    completeQueuedOperation();
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
            if (p->encodingName() == remotePayload->encodingName().upper()) {
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

void MediaEndpointPeerConnection::setRemoteDescription(RTCSessionDescription* description, VoidPromise&& promise)
{
    RefPtr<RTCSessionDescription> protectedDescription = description;
    RefPtr<WrappedVoidPromise> wrappedPromise = WrappedVoidPromise::create(WTF::move(promise));

    enqueueOperation([this, protectedDescription, wrappedPromise]() {
        queuedSetRemoteDescription(protectedDescription.get(), wrappedPromise->promise());
    });
}

void MediaEndpointPeerConnection::queuedSetRemoteDescription(RTCSessionDescription* description, VoidPromise& promise)
{
    if (m_client->internalSignalingState() == SignalingState::Closed)
        return;

    SessionDescription::Type descriptionType = parseDescriptionType(description->type());

    if (!remoteDescriptionTypeValidForState(descriptionType)) {
        // FIXME: Error type?
        promise.reject(DOMError::create("InvalidSessionDescriptionError (bad description type for current state)"));
        return;
    }

    String json = fromSDP(description->sdp());
    RefPtr<MediaEndpointConfiguration> parsedConfiguration = MediaEndpointConfigurationConversions::fromJSON(json);

    if (!parsedConfiguration) {
        // FIXME: Error type?
        promise.reject(DOMError::create("InvalidSessionDescriptionError (unable to parse description)"));
        completeQueuedOperation();
        return;
    }

    RtpSenderVector senders = m_client->getSenders();

    for (auto& mediaDescription : parsedConfiguration->mediaDescriptions()) {
        if (mediaDescription->type() != "audio" && mediaDescription->type() != "video")
            continue;

        mediaDescription->setPayloads(filterPayloads(mediaDescription->payloads(),
            mediaDescription->type() == "audio" ? m_defaultAudioPayloads : m_defaultVideoPayloads));
    }

    bool isInitiator = descriptionType == SessionDescription::Type::Answer;

    if (internalLocalDescription())
        updateSendSources(internalLocalDescription()->configuration()->mediaDescriptions(), parsedConfiguration->mediaDescriptions(), senders);

    if (m_mediaEndpoint->prepareToSend(parsedConfiguration.get(), isInitiator) == MediaEndpointPrepareResult::Failed) {
        // FIXME: Error type?
        promise.reject(DOMError::create("IncompatibleSessionDescriptionError (send configuration)"));
        completeQueuedOperation();
        return;
    }

    RefPtr<SessionDescription> newDescription = SessionDescription::create(descriptionType, WTF::move(parsedConfiguration));
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
    completeQueuedOperation();
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
    // FIXME: updateIce() might be renamed to setConfiguration(). It's also possible
    // that its current behavior with update deltas will change.
    m_mediaEndpoint->setConfiguration(createMediaEndpointInit(configuration));
}

void MediaEndpointPeerConnection::addIceCandidate(RTCIceCandidate* rtcCandidate, PeerConnection::VoidPromise&& promise)
{
    RefPtr<RTCIceCandidate> protectedCandidate = rtcCandidate;
    RefPtr<WrappedVoidPromise> wrappedPromise = WrappedVoidPromise::create(WTF::move(promise));

    enqueueOperation([this, protectedCandidate, wrappedPromise]() {
        queuedAddIceCandidate(protectedCandidate.get(), wrappedPromise->promise());
    });
}

void MediaEndpointPeerConnection::queuedAddIceCandidate(RTCIceCandidate* rtcCandidate, PeerConnection::VoidPromise& promise)
{
    if (!remoteDescription()) {
        // FIXME: Error type?
        promise.reject(DOMError::create("InvalidStateError (no remote description)"));
        completeQueuedOperation();
        return;
    }

    String json = iceCandidateFromSDP(rtcCandidate->candidate());
    RefPtr<IceCandidate> candidate = MediaEndpointConfigurationConversions::iceCandidateFromJSON(json);
    if (!candidate) {
        // FIXME: Error type?
        promise.reject(DOMError::create("SyntaxError (malformed candidate)"));
        completeQueuedOperation();
        return;
    }

    const MediaDescriptionVector& remoteMediaDescriptions = internalRemoteDescription()->configuration()->mediaDescriptions();
    unsigned mdescIndex = rtcCandidate->sdpMLineIndex();

    if (mdescIndex >= remoteMediaDescriptions.size()) {
        // FIXME: Error type?
        promise.reject(DOMError::create("InvalidSdpMlineIndex (sdpMLineIndex out of range"));
        completeQueuedOperation();
        return;
    }

    PeerMediaDescription& mdesc = *remoteMediaDescriptions[mdescIndex];
    mdesc.addIceCandidate(candidate.copyRef());

    m_mediaEndpoint->addRemoteCandidate(*candidate, mdescIndex, mdesc.iceUfrag(), mdesc.icePassword());

    promise.resolve(nullptr);
    completeQueuedOperation();
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

SessionDescription::Type MediaEndpointPeerConnection::parseDescriptionType(const String& typeName) const
{
    if (typeName == "offer")
        return SessionDescription::Type::Offer;
    if (typeName == "pranswer")
        return SessionDescription::Type::Pranswer;

    ASSERT(typeName == "answer");
    return SessionDescription::Type::Answer;
}

static String descriptionTypeToString(SessionDescription::Type type)
{
    switch (type) {
    case SessionDescription::Type::Offer:
        return ASCIILiteral("offer");
    case SessionDescription::Type::Pranswer:
        return ASCIILiteral("pranswer");
    case SessionDescription::Type::Answer:
        return ASCIILiteral("answer");
    case SessionDescription::Type::Rollback:
        return ASCIILiteral("rollback");
    }

    ASSERT_NOT_REACHED();
    return emptyString();
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
    if (!description)
        return nullptr;

    String json = MediaEndpointConfigurationConversions::toJSON(description->configuration());
    return RTCSessionDescription::create(descriptionTypeToString(description->type()), toSDP(json));
}

static String generateFingerprint(const String& certificate)
{
    Vector<String> certificateRows;
    Vector<uint8_t> der;

    der.reserveCapacity(certificate.length() * 3/4 + 2);
    certificate.split("\n", certificateRows);

    for (auto& row : certificateRows) {
        if (row.startsWith("-----"))
            continue;

        Vector<uint8_t> decodedRow;
        if (!base64Decode(row, decodedRow, Base64FailOnInvalidCharacterOrExcessPadding)) {
            ASSERT_NOT_REACHED();
            return emptyString();
        }
        der.appendVector(decodedRow);
    }

    std::unique_ptr<CryptoDigest> digest = CryptoDigest::create(CryptoAlgorithmIdentifier::SHA_256);
    if (!digest) {
        ASSERT_NOT_REACHED();
        return emptyString();
    }

    digest->addBytes(der.data(), der.size());
    Vector<uint8_t> fingerprintVector = digest->computeHash();

    StringBuilder fingerprint;
    for (unsigned i = 0; i < fingerprintVector.size(); ++i)
        fingerprint.append(String::format(i ? ":%02X" : "%02X", fingerprintVector[i]));

    return fingerprint.toString();
}

void MediaEndpointPeerConnection::gotDtlsCertificate(const String& certificate)
{
    m_dtlsFingerprint = generateFingerprint(certificate);
    completeQueuedOperation();
}

void MediaEndpointPeerConnection::gotIceCandidate(unsigned mdescIndex, RefPtr<IceCandidate>&& candidate)
{
    ASSERT(scriptExecutionContext()->isContextThread());

    PeerMediaDescription& mdesc = *internalLocalDescription()->configuration()->mediaDescriptions()[mdescIndex];
    mdesc.addIceCandidate(candidate.copyRef());

    // FIXME: should we still do this?
    if (!candidate->address().contains(':')) { // not IPv6
        if (candidate->componentId() == 1) { // RTP
            if (mdesc.address().isEmpty() || mdesc.address() == "0.0.0.0") {
                mdesc.setAddress(candidate->address());
                mdesc.setPort(candidate->port());
            }
        } else { // RTCP
            if (mdesc.rtcpAddress().isEmpty() || !mdesc.rtcpPort()) {
                mdesc.setRtcpAddress(candidate->address());
                mdesc.setRtcpPort(candidate->port());
            }
        }
    }

    String candidateString = MediaEndpointConfigurationConversions::iceCandidateToJSON(candidate.get());
    String sdpFragment = iceCandidateToSDP(candidateString);
    RefPtr<RTCIceCandidate> iceCandidate = RTCIceCandidate::create(sdpFragment, "", mdescIndex);
    m_client->fireEvent(RTCIceCandidateEvent::create(false, false, WTF::move(iceCandidate)));
}

void MediaEndpointPeerConnection::doneGatheringCandidates(unsigned mdescIndex)
{
    ASSERT(scriptExecutionContext()->isContextThread());

    const MediaDescriptionVector& mediaDescriptions = internalLocalDescription()->configuration()->mediaDescriptions();
    mediaDescriptions[mdescIndex]->setIceCandidateGatheringDone(true);

    for (auto& mdesc : mediaDescriptions) {
        if (!mdesc->iceCandidateGatheringDone())
            return;
    }

    m_client->fireEvent(RTCIceCandidateEvent::create(false, false, nullptr));
}

void MediaEndpointPeerConnection::gotRemoteSource(unsigned mdescIndex, RefPtr<RealtimeMediaSource>&& source)
{
    ASSERT(scriptExecutionContext()->isContextThread());

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

    RefPtr<MediaStreamTrackPrivate> trackPrivate = MediaStreamTrackPrivate::create(WTF::move(source), trackId);
    RefPtr<MediaStreamTrack> track = MediaStreamTrack::create(*m_client->context(), *trackPrivate);
    RefPtr<RTCRtpReceiver> receiver = RTCRtpReceiver::create(track.copyRef());

    m_client->fireEvent(RTCTrackEvent::create(eventNames().trackEvent, false, false, WTF::move(receiver), WTF::move(track)));
}

String MediaEndpointPeerConnection::toSDP(const String& json) const
{
    return sdpConversion("toSDP", json);
}

String MediaEndpointPeerConnection::fromSDP(const String& sdp) const
{
    return sdpConversion("fromSDP", sdp);
}

String MediaEndpointPeerConnection::iceCandidateToSDP(const String& json) const
{
    return sdpConversion("iceCandidateToSDP", json);
}

String MediaEndpointPeerConnection::iceCandidateFromSDP(const String& sdpFragment) const
{
    return sdpConversion("iceCandidateFromSDP", sdpFragment);
}

String MediaEndpointPeerConnection::sdpConversion(const String& functionName, const String& argument) const
{
    Document* document = downcast<Document>(m_client->context());

    if (!m_isolatedWorld)
        m_isolatedWorld = DOMWrapperWorld::create(JSDOMWindow::commonVM());

    ScriptController& scriptController = document->frame()->script();
    JSDOMGlobalObject* globalObject = JSC::jsCast<JSDOMGlobalObject*>(scriptController.globalObject(*m_isolatedWorld));
    JSC::ExecState* exec = globalObject->globalExec();
    JSC::JSLockHolder lock(exec);

    JSC::JSValue probeFunctionValue = globalObject->get(exec, JSC::Identifier::fromString(exec, "toSDP"));
    if (!probeFunctionValue.isFunction()) {
        URL scriptURL;
        scriptController.evaluateInWorld(ScriptSourceCode(SDPScriptResource::getString(), scriptURL), *m_isolatedWorld);
        if (exec->hadException()) {
            exec->clearException();
            return emptyString();
        }
    }

    JSC::JSValue functionValue = globalObject->get(exec, JSC::Identifier::fromString(exec, functionName));
    if (!functionValue.isFunction())
        return emptyString();

    JSC::JSObject* function = functionValue.toObject(exec);
    JSC::CallData callData;
    JSC::CallType callType = function->methodTable()->getCallData(function, callData);
    if (callType == JSC::CallTypeNone)
        return emptyString();

    JSC::MarkedArgumentBuffer argList;
    argList.append(JSC::jsString(exec, argument));

    JSC::JSValue result = JSC::call(exec, function, callType, callData, globalObject, argList);
    if (exec->hadException()) {
        printf("sdpConversion: js function (%s) threw\n", functionName.ascii().data());
        exec->clearException();
        return emptyString();
    }

    return result.isString() ? result.getString(exec) : emptyString();
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
