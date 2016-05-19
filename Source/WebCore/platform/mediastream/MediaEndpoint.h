/*
 * Copyright (C) 2015, 2016 Ericsson AB. All rights reserved.
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

#ifndef MediaEndpoint_h
#define MediaEndpoint_h

#if ENABLE(WEB_RTC)

#include "MediaEndpointConfiguration.h"
#include "RealtimeMediaSource.h"
#include <wtf/text/WTFString.h>

namespace WebCore {

class IceCandidate;
class MediaEndpoint;
class MediaEndpointClient;
class MediaEndpointSessionConfiguration;
class MediaPayload;
class PeerMediaDescription;
class RealtimeMediaSource;

typedef std::unique_ptr<MediaEndpoint> (*CreateMediaEndpoint)(MediaEndpointClient&);
typedef Vector<RefPtr<MediaPayload>> MediaPayloadVector;
typedef HashMap<String, RealtimeMediaSource*> RealtimeMediaSourceMap;

class MediaEndpoint {
public:
    WEBCORE_EXPORT static CreateMediaEndpoint create;
    virtual ~MediaEndpoint() { }

    enum class UpdateResult {
        Success,
        SuccessWithIceRestart,
        Failed
    };

    enum class IceTransportState {
        New = 1,
        Checking = 2,
        Connected = 3,
        Completed = 4,
        Failed = 5,
        Disconnected = 6,
        Closed = 7
    };

    virtual void setConfiguration(RefPtr<MediaEndpointConfiguration>&&) = 0;

    virtual void generateDtlsInfo() = 0;
    virtual MediaPayloadVector getDefaultAudioPayloads() = 0;
    virtual MediaPayloadVector getDefaultVideoPayloads() = 0;
    virtual MediaPayloadVector filterPayloads(const MediaPayloadVector& remotePayloads, const MediaPayloadVector& defaultPayloads) = 0;

    virtual UpdateResult updateReceiveConfiguration(MediaEndpointSessionConfiguration*, bool isInitiator) = 0;
    virtual UpdateResult updateSendConfiguration(MediaEndpointSessionConfiguration*, const RealtimeMediaSourceMap&, bool isInitiator) = 0;

    virtual void addRemoteCandidate(IceCandidate&, const String& mid, const String& ufrag, const String& password) = 0;

    virtual RefPtr<RealtimeMediaSource> createMutedRemoteSource(const String& mid, RealtimeMediaSource::Type) = 0;
    virtual void replaceSendSource(RealtimeMediaSource&, const String& mid) = 0;

    virtual void stop() = 0;
};

class MediaEndpointClient {
public:
    virtual void gotDtlsFingerprint(const String& fingerprint, const String& fingerprintFunction) = 0;
    virtual void gotIceCandidate(const String& mid, RefPtr<IceCandidate>&&) = 0;
    virtual void doneGatheringCandidates(const String& mid) = 0;
    virtual void iceTransportStateChanged(const String& mid, MediaEndpoint::IceTransportState) = 0;

    virtual ~MediaEndpointClient() { }
};

} // namespace WebCore

#endif // ENABLE(WEB_RTC)

#endif // MediaEndpoint_h
