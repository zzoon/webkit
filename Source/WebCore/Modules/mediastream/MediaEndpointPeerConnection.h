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

#ifndef MediaEndpointPeerConnection_h
#define MediaEndpointPeerConnection_h

#if ENABLE(MEDIA_STREAM)

#include "MediaEndpoint.h"
#include "MediaEndpointConfiguration.h"
#include "PeerConnectionBackend.h"
#include "SessionDescription.h"
#include <wtf/RefPtr.h>
#include <wtf/RefCounted.h>

namespace WebCore {

class DOMWrapperWorld;

typedef Vector<RefPtr<PeerMediaDescription>> MediaDescriptionVector;
typedef Vector<RefPtr<RTCRtpSender>> RtpSenderVector;

class MediaEndpointPeerConnection : public PeerConnectionBackend, public MediaEndpointClient {
public:
    MediaEndpointPeerConnection(PeerConnectionBackendClient*);
    ~MediaEndpointPeerConnection();

    void createOffer(const RefPtr<RTCOfferOptions>&, PeerConnection::SessionDescriptionPromise&&) override;
    void createAnswer(const RefPtr<RTCAnswerOptions>&, PeerConnection::SessionDescriptionPromise&&) override;

    void setLocalDescription(RTCSessionDescription*, PeerConnection::VoidPromise&&) override;
    RefPtr<RTCSessionDescription> localDescription() const override;
    RefPtr<RTCSessionDescription> currentLocalDescription() const override;
    RefPtr<RTCSessionDescription> pendingLocalDescription() const override;

    void setRemoteDescription(RTCSessionDescription*, PeerConnection::VoidPromise&&) override;
    RefPtr<RTCSessionDescription> remoteDescription() const override;
    RefPtr<RTCSessionDescription> currentRemoteDescription() const override;
    RefPtr<RTCSessionDescription> pendingRemoteDescription() const override;

    void setConfiguration(RTCConfiguration&) override;
    void addIceCandidate(RTCIceCandidate*, PeerConnection::VoidPromise&&) override;

    void stop() override;

    bool isNegotiationNeeded() const override { return m_negotiationNeeded; };
    void markAsNeedingNegotiation();
    void clearNegotiationNeededState() override { m_negotiationNeeded = false; };

private:

    void enqueueOperation(std::function<void ()>);
    void completeQueuedOperation();

    void queuedCreateOffer(const RefPtr<RTCOfferOptions>&, PeerConnection::SessionDescriptionPromise&);
    void queuedCreateAnswer(const RefPtr<RTCAnswerOptions>&, PeerConnection::SessionDescriptionPromise&);

    void queuedSetLocalDescription(RTCSessionDescription*, PeerConnection::VoidPromise&);
    void queuedSetRemoteDescription(RTCSessionDescription*, PeerConnection::VoidPromise&);

    void queuedAddIceCandidate(RTCIceCandidate*, PeerConnection::VoidPromise&);

    bool localDescriptionTypeValidForState(SessionDescription::Type) const;
    bool remoteDescriptionTypeValidForState(SessionDescription::Type) const;
    SessionDescription::Type parseDescriptionType(const String& typeName) const;

    SessionDescription* internalLocalDescription() const;
    SessionDescription* internalRemoteDescription() const;
    RefPtr<RTCSessionDescription> createRTCSessionDescription(SessionDescription*) const;

    // MediaEndpointClient
    virtual void gotDtlsCertificate(const String& certificate) override;
    virtual void gotIceCandidate(unsigned mdescIndex, RefPtr<IceCandidate>&&) override;
    virtual void doneGatheringCandidates(unsigned mdescIndex) override;
    virtual void gotRemoteSource(unsigned mdescIndex, RefPtr<RealtimeMediaSource>&&) override;

    String toSDP(const String& json) const;
    String fromSDP(const String& sdp) const;
    String iceCandidateToSDP(const String& json) const;
    String iceCandidateFromSDP(const String& sdpFragment) const;
    String sdpConversion(const String& functionName, const String& argument) const;

    PeerConnectionBackendClient* m_client;
    std::unique_ptr<MediaEndpoint> m_mediaEndpoint;

    Vector<std::function<void ()>> m_operationsQueue;

    mutable RefPtr<DOMWrapperWorld> m_isolatedWorld;

    Vector<RefPtr<MediaPayload>> m_defaultAudioPayloads;
    Vector<RefPtr<MediaPayload>> m_defaultVideoPayloads;

    String m_cname;
    String m_iceUfrag;
    String m_icePassword;
    String m_dtlsFingerprint;
    unsigned m_sdpSessionVersion;

    RefPtr<SessionDescription> m_currentLocalDescription;
    RefPtr<SessionDescription> m_pendingLocalDescription;

    RefPtr<SessionDescription> m_currentRemoteDescription;
    RefPtr<SessionDescription> m_pendingRemoteDescription;

    RefPtr<RTCConfiguration> m_configuration;

    bool m_negotiationNeeded;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

#endif // MediaEndpointPeerConnection_h
