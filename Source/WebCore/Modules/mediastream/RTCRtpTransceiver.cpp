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
#include "RTCRtpTransceiver.h"

#if ENABLE(MEDIA_STREAM)

#include "DOMError.h"
#include "Dictionary.h"
#include "ExceptionCode.h"
#include "JSDOMError.h"
#include "MediaStreamTrack.h"

namespace WebCore {

#define STRING_FUNCTION(name) \
    static const String& name##String() \
    { \
        static NeverDestroyed<const String> name { ASCIILiteral(#name) }; \
        return name; \
    }

STRING_FUNCTION(sendrecv)
STRING_FUNCTION(sendonly)
STRING_FUNCTION(recvonly)
STRING_FUNCTION(inactive)

Ref<RTCRtpTransceiver> RTCRtpTransceiver::create(RefPtr<RTCRtpSender>&& sender, RefPtr<RTCRtpReceiver>&& receiver)
{
    return adoptRef(*new RTCRtpTransceiver(WTFMove(sender), WTFMove(receiver)));
}

String RTCRtpTransceiver::getNextMid()
{
    static unsigned mid = 0;
    return String::number(++mid);
}

RTCRtpTransceiver::RTCRtpTransceiver(RefPtr<RTCRtpSender>&& sender, RefPtr<RTCRtpReceiver>&& receiver)
    : m_sender(sender)
    , m_receiver(receiver)
{
}

static bool parseDirectionality(const String& string, RTCRtpTransceiver::DirectionalityStatus& sendStatus, RTCRtpTransceiver::DirectionalityStatus& receiveStatus)
{
    if (string == sendrecvString()) {
        sendStatus = RTCRtpTransceiver::DirectionalityStatus::Enabled;
        receiveStatus = RTCRtpTransceiver::DirectionalityStatus::Enabled;
    } else if (string == sendonlyString()) {
        sendStatus = RTCRtpTransceiver::DirectionalityStatus::Enabled;
        receiveStatus = RTCRtpTransceiver::DirectionalityStatus::Disabled;
    } else if (string == recvonlyString()) {
        sendStatus = RTCRtpTransceiver::DirectionalityStatus::Disabled;
        receiveStatus = RTCRtpTransceiver::DirectionalityStatus::Enabled;
    } else if (string == inactiveString()) {
        sendStatus = RTCRtpTransceiver::DirectionalityStatus::Disabled;
        receiveStatus = RTCRtpTransceiver::DirectionalityStatus::Disabled;
    } else
        return false;
    return true;
}

bool RTCRtpTransceiver::configureWithDictionary(const Dictionary& dictionary)
{
    String directionality;
    if (dictionary.get("directionality", directionality)) {
        if (!parseDirectionality(directionality, m_sendStatus, m_receiveStatus))
            return false;
    }

    // FIMXE: fix streams
    return true;
}

const String& RTCRtpTransceiver::directionalityString() const
{
    if (m_sendStatus == DirectionalityStatus::Enabled)
        return m_receiveStatus == DirectionalityStatus::Enabled ? sendrecvString() : sendonlyString();
    return m_receiveStatus == DirectionalityStatus::Enabled ? recvonlyString() : inactiveString();
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
