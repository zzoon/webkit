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
#include "MediaStream.h"
#include "MediaStreamEvent.h"
#include "MediaStreamTrack.h"
#include "PeerConnectionBackend.h"
#include "PeerMediaDescription.h"
#include "RTCConfiguration.h"
#include "RTCIceCandidate.h"
#include "RTCIceCandidateEvent.h"
#include "RTCOfferAnswerOptions.h"
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

static RTCRtpTransceiver* matchTransceiver(const RtpTransceiverVector& transceivers, const std::function<bool(RTCRtpTransceiver&)>& matchFunction)
{
    for (auto& transceiver : transceivers) {
        if (matchFunction(*transceiver))
            return transceiver.get();
    }
    return nullptr;
}

static RTCRtpTransceiver* matchTransceiverByMid(const RtpTransceiverVector& transceivers, const String& mid)
{
    return matchTransceiver(transceivers, [&mid] (RTCRtpTransceiver& current) {
        return current.mid() == mid;
    });
}

static bool hasUnassociatedTransceivers(const RtpTransceiverVector& transceivers)
{
    return matchTransceiver(transceivers, [] (RTCRtpTransceiver& current) {
        return current.mid().isNull() && !current.stopped();
    });
}

static size_t indexOfMediaDescriptionWithTrackId(const MediaDescriptionVector& mediaDescriptions, const String& trackId)
{
    for (size_t i = 0; i < mediaDescriptions.size(); ++i) {
        if (mediaDescriptions[i]->mediaStreamTrackId() == trackId)
            return i;
    }
    return notFound;
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

    configurationSnapshot->setSessionVersion(m_sdpOfferSessionVersion++);

    RtpTransceiverVector transceivers = RtpTransceiverVector(m_client->getTransceivers());

    // Remove any transceiver objects from transceivers that can be matched to an existing media description.
    for (auto& mediaDescription : configurationSnapshot->mediaDescriptions()) {
        if (!mediaDescription->port()) {
            // This media description should be recycled.
            continue;
        }

        RTCRtpTransceiver* transceiver = matchTransceiverByMid(transceivers, mediaDescription->mid());
        if (!transceiver)
            continue;

        mediaDescription->setMode(transceiver->directionalityString());
        if (transceiver->hasActiveSender()) {
            RTCRtpSender* sender = transceiver->sender();
            mediaDescription->setMediaStreamId(sender->mediaStreamIds()[0]);
            mediaDescription->setMediaStreamTrackId(sender->trackId());
        }

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

    if (!internalRemoteDescription()) {
        // FIXME: Error type?
        promise.reject(DOMError::create("InvalidStateError (no remote description)"));
        return;
    }

    RefPtr<MediaEndpointSessionConfiguration> configurationSnapshot = internalLocalDescription() ?
        internalLocalDescription()->configuration()->clone() : MediaEndpointSessionConfiguration::create();

    configurationSnapshot->setSessionVersion(m_sdpAnswerSessionVersion++);

    RtpTransceiverVector transceivers = RtpTransceiverVector(m_client->getTransceivers());
    const MediaDescriptionVector& remoteMediaDescriptions = internalRemoteDescription()->configuration()->mediaDescriptions();

    for (unsigned i = 0; i < remoteMediaDescriptions.size(); ++i) {
        PeerMediaDescription& remoteMediaDescription = *remoteMediaDescriptions[i];

        RTCRtpTransceiver* transceiver = matchTransceiverByMid(transceivers, remoteMediaDescription.mid());
        if (!transceiver) {
            LOG_ERROR("Could not find a matching transceiver for remote description while creating answer");
            continue;
        }

        if (i >= configurationSnapshot->mediaDescriptions().size()) {
            RefPtr<PeerMediaDescription> newMediaDescription = PeerMediaDescription::create();

            RTCRtpSender& sender = *transceiver->sender();
            if (sender.track()) {
                if (sender.mediaStreamIds().size())
                    newMediaDescription->setMediaStreamId(sender.mediaStreamIds()[0]);
                newMediaDescription->setMediaStreamTrackId(sender.trackId());
                newMediaDescription->addSsrc(cryptographicallyRandomNumber());
            }

            newMediaDescription->setMode(transceiver->directionalityString());
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

        transceivers.removeFirst(transceiver);
    }

    // Unassociated (non-stopped) transceivers need to be negotiated in a follow-up offer.
    if (hasUnassociatedTransceivers(transceivers))
        markAsNeedingNegotiation();

    Ref<SessionDescription> description = SessionDescription::create(SessionDescription::Type::Answer, WTFMove(configurationSnapshot));
    promise.resolve(description->toRTCSessionDescription(*m_sdpProcessor));
}

static void updateSendSources(const MediaDescriptionVector& remoteMediaDescriptions, unsigned localMediaDescriptionCount, const RtpTransceiverVector& transceivers)
{
    for (unsigned i = 0; i < remoteMediaDescriptions.size() && i < localMediaDescriptionCount; ++i) {
        PeerMediaDescription& remoteMediaDescription = *remoteMediaDescriptions[i];
        if (remoteMediaDescription.type() != "audio" && remoteMediaDescription.type() != "video")
            continue;

        RTCRtpTransceiver* transceiver = matchTransceiverByMid(transceivers, remoteMediaDescription.mid());
        if (transceiver) {
            if (transceiver->hasActiveSender() && transceiver->sender()->track())
                remoteMediaDescription.setSource(&transceiver->sender()->track()->source());
            return;
        }
    }
}

void MediaEndpointPeerConnection::setLocalDescription(RTCSessionDescription& description, VoidPromise&& promise)
{
    RefPtr<RTCSessionDescription> protectedDescription = &description;
    RefPtr<WrappedVoidPromise> wrappedPromise = WrappedVoidPromise::create(WTFMove(promise));

    runTask([this, protectedDescription, wrappedPromise]() {
        setLocalDescriptionTask(*protectedDescription, wrappedPromise->promise());
    });
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

    const RtpTransceiverVector& transceivers = m_client->getTransceivers();
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
        updateSendSources(internalRemoteDescription()->configuration()->mediaDescriptions(), mediaDescriptions.size(), transceivers);

        if (m_mediaEndpoint->updateSendConfiguration(internalRemoteDescription()->configuration(), isInitiator) == MediaEndpoint::UpdateResult::Failed) {
            // FIXME: Error type?
            promise.reject(DOMError::create("IncompatibleSessionDescriptionError (send configuration)"));
            return;
        }
    }

    if (!hasUnassociatedTransceivers(transceivers))
        clearNegotiationNeededState();

    SignalingState newSignalingState;

    // Update state and local descriptions according to setLocal/RemoteDescription processing model
    switch (newDescription->type()) {
    case SessionDescription::Type::Offer:
        m_pendingLocalDescription = newDescription;
        newSignalingState = SignalingState::HaveLocalOffer;
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

    for (auto& mediaDescription : mediaDescriptions) {
        if (mediaDescription->type() != "audio" && mediaDescription->type() != "video")
            continue;

        mediaDescription->setPayloads(filterPayloads(mediaDescription->payloads(),
            mediaDescription->type() == "audio" ? m_defaultAudioPayloads : m_defaultVideoPayloads));
    }

    bool isInitiator = newDescription->type() == SessionDescription::Type::Answer;
    const RtpTransceiverVector& transceivers = m_client->getTransceivers();

    if (internalLocalDescription())
        updateSendSources(mediaDescriptions, internalLocalDescription()->configuration()->mediaDescriptions().size(), transceivers);

    if (m_mediaEndpoint->updateSendConfiguration(newDescription->configuration(), isInitiator) == MediaEndpoint::UpdateResult::Failed) {
        // FIXME: Error type?
        promise.reject(DOMError::create("IncompatibleSessionDescriptionError (send configuration)"));
        return;
    }

    // One legacy MediaStreamEvent will be fired for every new MediaStream created as this remote description is set.
    Vector<RefPtr<MediaStreamEvent>> legacyMediaStreamEvents;

    for (unsigned i = 0; i < mediaDescriptions.size(); ++i) {
        PeerMediaDescription* mediaDescription = mediaDescriptions[i].get();

        RTCRtpTransceiver* transceiver = matchTransceiverByMid(transceivers, mediaDescription->mid());
        if (!transceiver) {
            bool receiveOnlyFlag = false;

            if (mediaDescription->mode() == "sendrecv" || mediaDescription->mode() == "recvonly") {
                // Try to match an existing transceiver.
                transceiver = matchTransceiver(transceivers, [&mediaDescription] (RTCRtpTransceiver& current) {
                    return !current.stopped() && current.mid().isNull() && current.sender()->trackKind() == mediaDescription->type();
                });

                if (transceiver)
                    transceiver->setMid(mediaDescription->mid());
                else
                    receiveOnlyFlag = true;
            }

            if (!transceiver) {
                RefPtr<RTCRtpSender> sender = RTCRtpSender::create(mediaDescription->type(), Vector<String>(), m_client->senderClient());
                RefPtr<RTCRtpReceiver> receiver = createReceiver(mediaDescription->mid(), mediaDescription->type());

                Ref<RTCRtpTransceiver> newTransceiver = RTCRtpTransceiver::create(WTFMove(sender), WTFMove(receiver));
                newTransceiver->setMid(mediaDescription->mid());
                if (receiveOnlyFlag)
                    newTransceiver->setSenderStatus(RTCRtpTransceiver::Status::Inactive);

                transceiver = newTransceiver.ptr();
                m_client->addTransceiver(WTFMove(newTransceiver));
            }
        }

        if (mediaDescription->mode() == "sendrecv" || mediaDescription->mode() == "sendonly") {
            MediaStreamTrack& remoteTrack = *transceiver->receiver()->track();

            // FIXME: PeerMediaDescription should have a mediaStreamIds vector.
            Vector<String> mediaStreamIds;
            if (!mediaDescription->mediaStreamId().isEmpty())
                mediaStreamIds.append(mediaDescription->mediaStreamId());

            // A remote track can be associated with 0..* MediaStreams. We create a new stream on
            // an unrecognized stream id, and add the track if the stream has been created before.
            HashMap<String, RefPtr<MediaStream>> trackEventMediaStreams;
            for (auto& id : mediaStreamIds) {
                if (m_remoteStreamMap.contains(id)) {
                    RefPtr<MediaStream> stream = m_remoteStreamMap.get(id);
                    stream->addTrack(&remoteTrack);
                    trackEventMediaStreams.add(id, WTFMove(stream));
                } else {
                    Ref<MediaStream> newStream = MediaStream::create(*m_client->scriptExecutionContext(), MediaStreamTrackVector({ &remoteTrack }));
                    m_remoteStreamMap.add(id, newStream.copyRef());
                    legacyMediaStreamEvents.append(MediaStreamEvent::create(eventNames().addstreamEvent, false, false, newStream.copyRef()));
                    trackEventMediaStreams.add(id, WTFMove(newStream));
                }
            }

            Vector<RefPtr<MediaStream>> streams;
            copyValuesToVector(trackEventMediaStreams, streams);

            m_client->fireEvent(RTCTrackEvent::create(eventNames().trackEvent, false, false,
                transceiver->receiver(), &remoteTrack, WTFMove(streams), transceiver));
        }
    }

    // Fire legacy addstream events.
    for (auto& event : legacyMediaStreamEvents)
        m_client->fireEvent(*event);

    SignalingState newSignalingState;

    // Update state and local descriptions according to setLocal/RemoteDescription processing model
    switch (newDescription->type()) {
    case SessionDescription::Type::Offer:
        m_pendingRemoteDescription = newDescription;
        newSignalingState = SignalingState::HaveRemoteOffer;
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
    if (!internalRemoteDescription()) {
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

Vector<RefPtr<MediaStream>> MediaEndpointPeerConnection::getRemoteStreams() const
{
    Vector<RefPtr<MediaStream>> remoteStreams;
    copyValuesToVector(m_remoteStreamMap, remoteStreams);
    return remoteStreams;
}

RefPtr<RTCRtpReceiver> MediaEndpointPeerConnection::createReceiver(const String& transceiverMid, const String& trackKind)
{
    RealtimeMediaSource::Type sourceType = trackKind == "audio" ? RealtimeMediaSource::Type::Audio : RealtimeMediaSource::Type::Video;

    // Create a muted remote source that will be unmuted once media starts arriving.
    RefPtr<RealtimeMediaSource> remoteSource = m_mediaEndpoint->createMutedRemoteSource(transceiverMid, sourceType);
    // FIXME: Revisit when discussion about receiver track id concludes.
    RefPtr<MediaStreamTrackPrivate> remoteTrackPrivate = MediaStreamTrackPrivate::create(WTFMove(remoteSource));
    RefPtr<MediaStreamTrack> remoteTrack = MediaStreamTrack::create(*m_client->scriptExecutionContext(), *remoteTrackPrivate);

    return RTCRtpReceiver::create(WTFMove(remoteTrack));
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

void MediaEndpointPeerConnection::doneGatheringCandidates(const String& mid)
{
    ASSERT(isMainThread());

    RtpTransceiverVector transceivers = RtpTransceiverVector(m_client->getTransceivers());

    RTCRtpTransceiver* notifyingTransceiver = matchTransceiverByMid(transceivers, mid);
    ASSERT(notifyingTransceiver);
    notifyingTransceiver->iceTransport().setGatheringState(RTCIceTransport::GatheringState::Complete);

    // Determine if the script needs to be notified.
    RTCRtpTransceiver* stillGatheringTransceiver = matchTransceiver(transceivers, [] (RTCRtpTransceiver& current) {
        return !current.stopped() && !current.mid().isNull()
            && current.iceTransport().gatheringState() != RTCIceTransport::GatheringState::Complete;
    });
    if (!stillGatheringTransceiver)
        m_client->updateIceGatheringState(IceGatheringState::Complete);
}

static RTCIceTransport::TransportState deriveAggregatedIceConnectionState(Vector<RTCIceTransport::TransportState>& states)
{
    unsigned newCount = 0;
    unsigned checkingCount = 0;
    unsigned connectedCount = 0;
    unsigned completedCount = 0;
    unsigned failedCount = 0;
    unsigned disconnectedCount = 0;
    unsigned closedCount = 0;

    for (auto& state : states) {
        switch (state) {
        case RTCIceTransport::TransportState::New: ++newCount; break;
        case RTCIceTransport::TransportState::Checking: ++checkingCount; break;
        case RTCIceTransport::TransportState::Connected: ++connectedCount; break;
        case RTCIceTransport::TransportState::Completed: ++completedCount; break;
        case RTCIceTransport::TransportState::Failed: ++failedCount; break;
        case RTCIceTransport::TransportState::Disconnected: ++disconnectedCount; break;
        case RTCIceTransport::TransportState::Closed: ++closedCount; break;
        }
    }

    // The aggregated RTCIceConnectionState is derived from the RTCIceTransportState of all RTCIceTransports.
    if (newCount > 0 && !checkingCount && !failedCount && !disconnectedCount)
        return RTCIceTransport::TransportState::New;

    if (checkingCount > 0 && !failedCount && !disconnectedCount)
        return RTCIceTransport::TransportState::Checking;

    if ((connectedCount + completedCount + closedCount) == states.size() && connectedCount > 0)
        return RTCIceTransport::TransportState::Connected;

    if ((completedCount + closedCount) == states.size() && completedCount > 0)
        return RTCIceTransport::TransportState::Completed;

    if (failedCount > 0)
        return RTCIceTransport::TransportState::Failed;

    if (disconnectedCount > 0) // Any failed caught above.
        return RTCIceTransport::TransportState::Disconnected;

    if (closedCount == states.size())
        return RTCIceTransport::TransportState::Closed;

    ASSERT_NOT_REACHED();
    return RTCIceTransport::TransportState::New;
}

void MediaEndpointPeerConnection::iceTransportStateChanged(const String& mid, MediaEndpoint::IceTransportState mediaEndpointIceTransportState)
{
    ASSERT(isMainThread());

    RTCRtpTransceiver* transceiver = matchTransceiverByMid(m_client->getTransceivers(), mid);
    ASSERT(transceiver);

    RTCIceTransport::TransportState transportState = static_cast<RTCIceTransport::TransportState>(mediaEndpointIceTransportState);
    transceiver->iceTransport().setTransportState(transportState);

    // Determine if the script needs to be notified.
    Vector<RTCIceTransport::TransportState> transportStates;
    for (auto& transceiver : m_client->getTransceivers())
        transportStates.append(transceiver->iceTransport().transportState());

    RTCIceTransport::TransportState derivedState = deriveAggregatedIceConnectionState(transportStates);
    m_client->updateIceConnectionState(static_cast<IceConnectionState>(derivedState));
}

} // namespace WebCore

#endif // ENABLE(WEB_RTC)
