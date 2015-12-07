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

#ifndef MockMediaEndpoint_h
#define MockMediaEndpoint_h

#if ENABLE(MEDIA_STREAM)

#include "MediaEndpoint.h"

namespace WebCore {

class MockMediaEndpoint : public MediaEndpoint {
public:
    WEBCORE_EXPORT static std::unique_ptr<MediaEndpoint> create(MediaEndpointClient*);

    MockMediaEndpoint(MediaEndpointClient*);
    ~MockMediaEndpoint();

    virtual void setConfiguration(RefPtr<MediaEndpointInit>&&) override;

    virtual void getDtlsFingerprint() override;
    virtual Vector<RefPtr<MediaPayload>> getDefaultAudioPayloads() override;
    virtual Vector<RefPtr<MediaPayload>> getDefaultVideoPayloads() override;

    virtual MediaEndpointPrepareResult prepareToReceive(MediaEndpointConfiguration*, bool isInitiator) override;
    virtual MediaEndpointPrepareResult prepareToSend(MediaEndpointConfiguration*, bool isInitiator) override;

    virtual void addRemoteCandidate(IceCandidate&, unsigned mdescIndex, const String& ufrag, const String& password) override;

    virtual void replaceSendSource(RealtimeMediaSource&, unsigned mdescIndex) override;

    virtual void stop() override;

private:
    MediaEndpointClient* m_client;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

#endif // MockMediaEndpoint_h
