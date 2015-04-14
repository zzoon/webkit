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
#include "RTCConfiguration.h"

#if ENABLE(MEDIA_STREAM)

// #include "Dictionary.h"
// #include "ExceptionCode.h"

namespace WebCore {

PassRefPtr<RTCConfiguration> RTCConfiguration::create()
{
    return adoptRef(new RTCConfiguration());
}

RTCConfiguration::RTCConfiguration()
    : m_private(RTCConfigurationPrivate::create())
{
}

void RTCConfiguration::appendServer(PassRefPtr<RTCIceServer> server)
{
    m_private->appendServer(server->privateServer());
}

PassRefPtr<RTCIceServer> RTCConfiguration::server(size_t index)
{
    RTCIceServerPrivate* server = m_private->server(index);
    if (!server)
        return nullptr;

    return RTCIceServer::create(server);
}

Vector<RefPtr<RTCIceServer>> RTCConfiguration::iceServers() const
{
    Vector<RefPtr<RTCIceServer>> servers;
    Vector<RefPtr<RTCIceServerPrivate>> privateServers = m_private->iceServers();

    for (auto iter = privateServers.begin(); iter != privateServers.end(); ++iter)
        servers.append(RTCIceServer::create(*iter));

    return servers;
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
