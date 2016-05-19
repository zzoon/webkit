/*
 * Copyright (C) 2016 Ericsson AB. All rights reserved.
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

#if ENABLE(WEB_RTC)

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
    : m_direction(sendrecvString())
    , m_sender(sender)
    , m_receiver(receiver)
    , m_iceTransport(RTCIceTransport::create())
{
}

static bool isRTCRtpTransceiverDirectionEnumValue(const String& string)
{
    return string == sendrecvString() || string == sendonlyString()
        || string == recvonlyString() || string == inactiveString();
}

bool RTCRtpTransceiver::configureWithDictionary(const Dictionary& dictionary)
{
    String direction;
    if (dictionary.get("direction", direction)) {
        if (!isRTCRtpTransceiverDirectionEnumValue(direction))
            return false;
        m_direction = direction;
    }

    // FIMXE: fix streams
    return true;
}

bool RTCRtpTransceiver::hasSendingDirection() const
{
    return m_direction == sendrecvString() || m_direction == sendonlyString();
}

void RTCRtpTransceiver::enableSendingDirection()
{
    if (m_direction == recvonlyString())
        m_direction = sendrecvString();
    else if (m_direction == inactiveString())
        m_direction = sendonlyString();
}

void RTCRtpTransceiver::disableSendingDirection()
{
    if (m_direction == sendrecvString())
        m_direction = recvonlyString();
    else if (m_direction == sendonlyString())
        m_direction = inactiveString();
}

} // namespace WebCore

#endif // ENABLE(WEB_RTC)
