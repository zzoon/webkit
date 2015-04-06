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
#include "MediaEndpointOwr.h"

#include "MediaEndpointConfiguration.h"
#include "OpenWebRTCUtilities.h"
#include <owr/owr.h>
#include <wtf/text/CString.h>

namespace WebCore {

static void gotCandidate(OwrMediaSession*, OwrCandidate*, MediaEndpointOwr*);
static void candidateGatheringDone(OwrMediaSession*, MediaEndpointOwr*);

static char* iceCandidateTypes[] = { "host", "srflx", "relay", nullptr };

static std::unique_ptr<MediaEndpoint> createMediaEndpointOwr(MediaEndpointClient* client)
{
    return std::unique_ptr<MediaEndpoint>(new MediaEndpointOwr(client));
}

CreateMediaEndpoint MediaEndpoint::create = createMediaEndpointOwr;

MediaEndpointOwr::MediaEndpointOwr(MediaEndpointClient* client)
    : m_transportAgent(nullptr)
    , m_client(client)
    , m_numberOfReceivePreparedMediaSessions(0)
    , m_numberOfSendPreparedMediaSessions(0)
{
    initializeOpenWebRTC();
}

MediaEndpointOwr::~MediaEndpointOwr()
{
    if (m_transportAgent)
        g_object_unref(m_transportAgent);
}

void MediaEndpointOwr::setConfiguration(RefPtr<RTCConfigurationPrivate>&& configuration)
{
    m_configuration = configuration;
}

void MediaEndpointOwr::prepareToReceive(MediaEndpointConfiguration* configuration, bool isInitiator)
{
    Vector<String> dtlsRoles;
    for (unsigned i = m_mediaSessions.size(); i < configuration->mediaDescriptions().size(); ++i)
        dtlsRoles.append(configuration->mediaDescriptions()[i]->dtlsSetup());

    ensureTransportAgentAndMediaSessions(isInitiator, dtlsRoles);

    // prepare the new media sessions
    for (unsigned i = m_numberOfReceivePreparedMediaSessions; i < m_mediaSessions.size(); ++i)
        prepareMediaSession(i, m_mediaSessions[i], configuration->mediaDescriptions()[i].get());

    m_numberOfReceivePreparedMediaSessions = m_mediaSessions.size();
}

void MediaEndpointOwr::prepareToSend(MediaEndpointConfiguration* configuration, bool isInitiator)
{
    printf("-> MediaEndpointOwr::prepareToSend\n");
}

void MediaEndpointOwr::addRemoteCandidate(IceCandidate* candidate)
{
    OwrCandidateType candidateType;
    OwrComponentType componentType = (OwrComponentType) candidate->componentId();

    if (candidate->type() == "host")
        candidateType = OWR_CANDIDATE_TYPE_HOST;
    else if (candidate->type() == "srflx")
        candidateType = OWR_CANDIDATE_TYPE_SERVER_REFLEXIVE;
    else
        candidateType = OWR_CANDIDATE_TYPE_RELAY;

    OwrCandidate* remoteCandidate = owr_candidate_new(candidateType, componentType);

    printf("MediaEndpointOwr::addRemoteCandidate: created candidate: %p\n", remoteCandidate);
}

void MediaEndpointOwr::stop()
{
    printf("MediaEndpointOwr::stop\n");
}

unsigned MediaEndpointOwr::mediaSessionIndex(OwrMediaSession* mediaSession) const
{
    unsigned index = m_mediaSessions.find(mediaSession);
    ASSERT(index != notFound);
    return index;
}

void MediaEndpointOwr::dispatchNewIceCandidate(unsigned mediaSessionIndex, RefPtr<IceCandidate>&& iceCandidate)
{
    m_client->gotIceCandidate(mediaSessionIndex, WTF::move(iceCandidate));
}

void MediaEndpointOwr::dispatchGatheringDone(unsigned mediaSessionIndex)
{
    m_client->doneGatheringCandidates(mediaSessionIndex);
}

void MediaEndpointOwr::prepareMediaSession(unsigned mdescIndex, OwrMediaSession* mediaSession, PeerMediaDescription*)
{
    g_signal_connect(mediaSession, "on-new-candidate", G_CALLBACK(gotCandidate), this);
    g_signal_connect(mediaSession, "on-candidate-gathering-done", G_CALLBACK(candidateGatheringDone), this);

    owr_transport_agent_add_session(m_transportAgent, OWR_SESSION(mediaSession));
}

void MediaEndpointOwr::ensureTransportAgentAndMediaSessions(bool isInitiator, const Vector<String>& newMediaSessionDtlsRoles)
{
    if (!m_transportAgent) {
        m_transportAgent = owr_transport_agent_new(false);

        for (unsigned i = 0; i < m_configuration->numberOfServers(); ++i) {
            RTCIceServerPrivate* server = m_configuration->server(i);

            // FIXME: parse url type and port
            owr_transport_agent_add_helper_server(m_transportAgent, OWR_HELPER_SERVER_TYPE_STUN,
                server->urls()[0].ascii().data(), 3478, nullptr, nullptr);
        }
    }

    g_object_set(m_transportAgent, "ice-controlling-mode", isInitiator, nullptr);

    for (auto role : newMediaSessionDtlsRoles)
        m_mediaSessions.append(owr_media_session_new(role == "active"));
}

static void gotCandidate(OwrMediaSession* mediaSession, OwrCandidate* candidate, MediaEndpointOwr* mediaEndpoint)
{
    OwrCandidateType candidateType;
    OwrComponentType componentType;
    OwrTransportType transportType;
    gchar* foundation;
    gchar* address;
    gchar* relatedAddress;
    gint port, priority, relatedPort;

    g_object_get(candidate, "type", &candidateType,
        "component-type", &componentType,
        "foundation", &foundation,
        "transport-type", &transportType,
        "address", &address,
        "port", &port,
        "priority", &priority,
        "base-address", &relatedAddress,
        "base-port", &relatedPort, nullptr);

    printf("candidateType: %d, foundation: %s, address: %s, port %d\n", candidateType, foundation, address, port);

    RefPtr<IceCandidate> iceCandidate = IceCandidate::create();
    iceCandidate->setType(iceCandidateTypes[candidateType]);
    iceCandidate->setFoundation(foundation);
    iceCandidate->setTransport(transportType == OWR_TRANSPORT_TYPE_UDP ? "UDP" : "TCP");
    // FIXME: set the rest

    mediaEndpoint->dispatchNewIceCandidate(mediaEndpoint->mediaSessionIndex(mediaSession), WTF::move(iceCandidate));
}

static void candidateGatheringDone(OwrMediaSession* mediaSession, MediaEndpointOwr* mediaEndpoint)
{
    mediaEndpoint->dispatchGatheringDone(mediaEndpoint->mediaSessionIndex(mediaSession));
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
