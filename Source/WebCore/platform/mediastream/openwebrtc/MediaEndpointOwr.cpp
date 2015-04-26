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

static void gotCandidate(OwrSession*, OwrCandidate*, MediaEndpointOwr*);
static void candidateGatheringDone(OwrSession*, MediaEndpointOwr*);
static void gotDtlsCertificate(OwrSession*, GParamSpec*, MediaEndpointOwr*);
static void gotSendSsrc(OwrMediaSession*, GParamSpec*, MediaEndpointOwr*);
static void gotIncomingSource(OwrMediaSession*, OwrMediaSource*, MediaEndpointOwr*);

static const Vector<String> candidateTypes = { "host", "srflx", "prflx", "relay" };
static const Vector<String> candidateTcpTypes = { "", "active", "passive", "so" };

static std::unique_ptr<MediaEndpoint> createMediaEndpointOwr(MediaEndpointClient* client)
{
    return std::unique_ptr<MediaEndpoint>(new MediaEndpointOwr(client));
}

CreateMediaEndpoint MediaEndpoint::create = createMediaEndpointOwr;

MediaEndpointOwr::MediaEndpointOwr(MediaEndpointClient* client)
    : m_transportAgent(nullptr)
    , m_client(client)
    , m_numberOfReceivePreparedSessions(0)
    , m_numberOfSendPreparedSessions(0)
{
    initializeOpenWebRTC();
}

MediaEndpointOwr::~MediaEndpointOwr()
{
    if (m_transportAgent)
        g_object_unref(m_transportAgent);
}

void MediaEndpointOwr::setConfiguration(RefPtr<MediaEndpointInit>&& configuration)
{
    m_configuration = configuration;
}

void MediaEndpointOwr::prepareToReceive(MediaEndpointConfiguration* configuration, bool isInitiator)
{
    Vector<SessionConfig> sessionConfigs;
    for (unsigned i = m_sessions.size(); i < configuration->mediaDescriptions().size(); ++i) {
        SessionConfig config;
        config.type = SessionTypeMedia;
        config.isDtlsClient = configuration->mediaDescriptions()[i]->dtlsSetup() == "active";
        sessionConfigs.append(WTF::move(config));
    }

    ensureTransportAgentAndSessions(isInitiator, sessionConfigs);

    // Prepare the new sessions.
    for (unsigned i = m_numberOfReceivePreparedSessions; i < m_sessions.size(); ++i) {
        prepareMediaSession(OWR_MEDIA_SESSION(m_sessions[i]), configuration->mediaDescriptions()[i].get(), isInitiator);
        owr_transport_agent_add_session(m_transportAgent, m_sessions[i]);
    }

    m_numberOfReceivePreparedSessions = m_sessions.size();
}

void MediaEndpointOwr::prepareToSend(MediaEndpointConfiguration*, bool)
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

unsigned MediaEndpointOwr::sessionIndex(OwrSession* session) const
{
    unsigned index = m_sessions.find(session);
    ASSERT(index != notFound);
    return index;
}

void MediaEndpointOwr::dispatchNewIceCandidate(unsigned sessionIndex, RefPtr<IceCandidate>&& iceCandidate, const String& ufrag, const String& password)
{
    m_client->gotIceCandidate(sessionIndex, WTF::move(iceCandidate), ufrag, password);
}

void MediaEndpointOwr::dispatchGatheringDone(unsigned sessionIndex)
{
    m_client->doneGatheringCandidates(sessionIndex);
}

void MediaEndpointOwr::dispatchDtlsCertificate(unsigned sessionIndex, const String& certificate)
{
    m_client->gotDtlsCertificate(sessionIndex, certificate);
}

void MediaEndpointOwr::dispatchSendSSRC(unsigned sessionIndex, const String& ssrc, const String& cname)
{
    m_client->gotSendSSRC(sessionIndex, ssrc, cname);
}

void MediaEndpointOwr::prepareSession(OwrSession* session, PeerMediaDescription*)
{
    g_signal_connect(session, "on-new-candidate", G_CALLBACK(gotCandidate), this);
    g_signal_connect(session, "on-candidate-gathering-done", G_CALLBACK(candidateGatheringDone), this);
    g_signal_connect(session, "notify::dtls-certificate", G_CALLBACK(gotDtlsCertificate), this);
}

void MediaEndpointOwr::prepareMediaSession(OwrMediaSession* mediaSession, PeerMediaDescription* mediaDescription, bool isInitiator)
{
    prepareSession(OWR_SESSION(mediaSession), mediaDescription);

    bool useRtpMux = !isInitiator && mediaDescription->rtcpMux();
    g_object_set(mediaSession, "rtcp-mux", useRtpMux, nullptr);

    g_signal_connect(mediaSession, "notify::send-ssrc", G_CALLBACK(gotSendSsrc), this);
    g_signal_connect(mediaSession, "on-incoming-source", G_CALLBACK(gotIncomingSource), this);
}

void MediaEndpointOwr::ensureTransportAgentAndSessions(bool isInitiator, const Vector<SessionConfig>& sessionConfigs)
{
    if (!m_transportAgent) {
        m_transportAgent = owr_transport_agent_new(false);

        for (auto& server : m_configuration->iceServers()) {
            // FIXME: parse url type and port
            owr_transport_agent_add_helper_server(m_transportAgent, OWR_HELPER_SERVER_TYPE_STUN,
                server->urls()[0].ascii().data(), 3478, nullptr, nullptr);
        }
    }

    g_object_set(m_transportAgent, "ice-controlling-mode", isInitiator, nullptr);

    for (auto& config : sessionConfigs)
        m_sessions.append(OWR_SESSION(owr_media_session_new(config.isDtlsClient)));
}

static void gotCandidate(OwrSession* session, OwrCandidate* candidate, MediaEndpointOwr* mediaEndpoint)
{
    OwrCandidateType candidateType;
    gchar* foundation;
    OwrComponentType componentId;
    OwrTransportType transportType;
    gint priority;
    gchar* address;
    guint port;
    gchar* relatedAddress;
    guint relatedPort;
    gchar* ufrag;
    gchar* password;

    g_object_get(candidate, "type", &candidateType,
        "foundation", &foundation,
        "component-type", &componentId,
        "transport-type", &transportType,
        "priority", &priority,
        "address", &address,
        "port", &port,
        "base-address", &relatedAddress,
        "base-port", &relatedPort,
        "ufrag", &ufrag,
        "password", &password,
        nullptr);

    ASSERT(candidateType >= 0 && candidateType < candidateTypes.size());
    ASSERT(transportType >= 0 && transportType < candidateTcpTypes.size());

    RefPtr<IceCandidate> iceCandidate = IceCandidate::create();
    iceCandidate->setType(candidateTypes[candidateType]);
    iceCandidate->setFoundation(foundation);
    iceCandidate->setComponentId(componentId);
    iceCandidate->setPriority(priority);
    iceCandidate->setAddress(address);
    iceCandidate->setPort(port);

    if (transportType == OWR_TRANSPORT_TYPE_UDP)
        iceCandidate->setTransport("UDP");
    else {
        iceCandidate->setTransport("TCP");
        iceCandidate->setTcpType(candidateTcpTypes[transportType]);
    }

    if (candidateType == OWR_CANDIDATE_TYPE_HOST) {
        iceCandidate->setRelatedAddress(relatedAddress);
        iceCandidate->setRelatedPort(relatedPort);
    }

    mediaEndpoint->dispatchNewIceCandidate(mediaEndpoint->sessionIndex(session), WTF::move(iceCandidate), String(ufrag), String(password));

    g_free(foundation);
    g_free(address);
    g_free(relatedAddress);
    g_free(ufrag);
    g_free(password);
}

static void candidateGatheringDone(OwrSession* session, MediaEndpointOwr* mediaEndpoint)
{
    mediaEndpoint->dispatchGatheringDone(mediaEndpoint->sessionIndex(session));
}

static void gotDtlsCertificate(OwrSession* session, GParamSpec*, MediaEndpointOwr* mediaEndpoint)
{
    gchar* pem;
    g_object_get(session, "dtls-certificate", &pem, nullptr);

    String certificate(pem);
    g_free(pem);

    mediaEndpoint->dispatchDtlsCertificate(mediaEndpoint->sessionIndex(session), certificate);
}

static void gotSendSsrc(OwrMediaSession* mediaSession, GParamSpec*, MediaEndpointOwr* mediaEndpoint)
{
    gchar* cname;
    g_object_get(mediaSession, "cname", &cname, nullptr);

    // FIXME: fix send-ssrc
    mediaEndpoint->dispatchSendSSRC(mediaEndpoint->sessionIndex(OWR_SESSION(mediaSession)), "fix me", String(cname));

    g_free(cname);
}

static void gotIncomingSource(OwrMediaSession*, OwrMediaSource*, MediaEndpointOwr*)
{
    printf("-> gotIncomingSource\n");
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
