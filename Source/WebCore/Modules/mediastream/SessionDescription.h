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

#ifndef SessionDescription_h
#define SessionDescription_h

#if ENABLE(WEB_RTC)

#include "MediaEndpointSessionConfiguration.h"
#include "RTCSessionDescription.h"
#include <wtf/RefCounted.h>
#include <wtf/RefPtr.h>

namespace WebCore {

class MediaEndpointSessionConfiguration;
class SDPProcessor;
class DOMError;

class SessionDescription : public RefCounted<SessionDescription> {
public:
    enum class Type {
        Offer = 1,
        Pranswer = 2,
        Answer = 3,
        Rollback = 4
    };

    static Ref<SessionDescription> create(Type type, RefPtr<MediaEndpointSessionConfiguration>&& configuration);
    static RefPtr<SessionDescription> create(RefPtr<RTCSessionDescription>&&, const SDPProcessor&, RefPtr<DOMError>&);
    virtual ~SessionDescription() { }

    RefPtr<RTCSessionDescription> toRTCSessionDescription(const SDPProcessor&) const;

    Type type() const { return m_type; }
    const String& typeString() const;
    MediaEndpointSessionConfiguration* configuration() const { return m_configuration.get(); }

    bool isLaterThan(SessionDescription* other) const;

private:
    SessionDescription(Type type, RefPtr<MediaEndpointSessionConfiguration>&& configuration, RefPtr<RTCSessionDescription>&& rtcDescription)
        : m_type(type)
        , m_configuration(configuration)
        , m_rtcDescription(WTFMove(rtcDescription))
    { }

    Type m_type;
    RefPtr<MediaEndpointSessionConfiguration> m_configuration;

    RefPtr<RTCSessionDescription> m_rtcDescription;
};

} // namespace WebCore

#endif // ENABLE(WEB_RTC)

#endif // SessionDescription_h
