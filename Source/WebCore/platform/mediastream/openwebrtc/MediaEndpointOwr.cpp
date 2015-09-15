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
#include "MediaPayload.h"
#include "OpenWebRTCUtilities.h"
#include "RealtimeMediaSourceOwr.h"
#include <owr/owr.h>
#include <owr/owr_audio_payload.h>
#include <owr/owr_video_payload.h>
#include <wtf/text/CString.h>

namespace WebCore {

static void gotCandidate(OwrSession*, OwrCandidate*, MediaEndpointOwr*);
static void candidateGatheringDone(OwrSession*, MediaEndpointOwr*);
static void gotIncomingSource(OwrMediaSession*, OwrMediaSource*, MediaEndpointOwr*);

static const Vector<String> candidateTypes = { "host", "srflx", "prflx", "relay" };
static const Vector<String> candidateTcpTypes = { "", "active", "passive", "so" };
static const Vector<String> codecTypes = { "NONE", "PCMU", "PCMA", "OPUS", "H264", "VP8" };

static const gchar* dtlsKey =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "MIIEpAIBAAKCAQEAx4npCHPrreO5YSUYrZXjN/JTiLaaqbq9h8sSaF5lke64r8JO\n"
    "apGWXaLbMcUXfLkFtL4hydo0fyki8iN+oPuMJjEu0c/DX/N1N57wjnHFNY9mXJNx\n"
    "hL4Ev64PKxBrjTx/3CKMR6u8ssi0lIcnRjw9/bMKvedWl1S9wEbJeszd1rhLwAwD\n"
    "5euqYkzLUtlRthkines86E1zHKVofr0NED22RkL+WgC2q1Y6Ic09wjAqCK+4QVsX\n"
    "4XqFmOqYT/I1sb+JPZaIFOaowWWXaBxdKXQ4WbLqm25wA1ilyy1tMUsbnl8Sxouq\n"
    "F0dj5MdsKtNBckueV3Ex38fgCyz7EGhyMtqqzQIDAQABAoIBAGXcCcCN5F6FJEnp\n"
    "9PoPzMjvhCMDDFregAgE6yWqInFnipH2P695GGg+TWTPttitXrNQZ9Ex+aB8MGGp\n"
    "Kugk4PtSGhNy2sptboXxNd2RSFm6FUfm4IkhsyziPs39+NlFbAPFAxVHHvGpMT2s\n"
    "7KiW8hJDRpWAtZxU3vR7bjiowgnsbbtJ/ngmJ56wEgVr/t0fjimPdY7rGmX+o/mc\n"
    "QbY5+aN29EJiLtVqcQx7DujObHnDCs8Y1WxWChd4Ai3hFpCgfRMGBN7e3rUzZNTf\n"
    "GZmIiABOW/SKwS9Kjz9rZJLlYtbCZsl+/XGwpMEBISnnvKK07VZA6DIbrCKlk9Hb\n"
    "sIVHRgUCgYEA5nRghJHljO933LooUurF1kwPw6XigcFlwq9mOSk1USseEgEhooeS\n"
    "L4V6BuaQ6j3RhRJTb4kr22KqRNRWEB2Mk0eb71s0+ZdGT5hakbSWKQgtm9z0Zuup\n"
    "QJk+dle4TRN5zJMWT1o6hKH9GwwlkifHfvJoK0rzY4RsMn5uDt+N24cCgYEA3ag7\n"
    "QEI+TF1W55dOZWxCsOHjMZZXMLzfmWGLpcOyq3qPnj9fDtEi+iTcMOXhhYgxB3tO\n"
    "+I16XNKY/FWTvc2/979dh/AEospk3kHCEQ4NnvrKFw2UD/LL5SczSiXrIQfgAWMA\n"
    "n2FHQQbEsu+cgG8eioO9u8o4Mt138Jo+/llS5AsCgYEA1okseP2hJuyfNvqOI3Kv\n"
    "remtG0PIc2bpJq5GiZwVKHTtT3GCMF3o9xhZGyd1bLsT27/NsJ2QGHHndJ//Zo07\n"
    "mrglMFRGIrxzFhIM7muhBp24Z8rwMwfbzmlavqy2w/oHfyzGriSfKW3rxEwwhblG\n"
    "fKWJ2BO0NMbIOtF7/5iZ5O0CgYEAlmQ4n2bSwhlqh4O/q00DCuSYs+Jfki/0PitT\n"
    "Bst7BKIJo8M3ieQYKUStKXgvxdwb+AmQEVBcv3IcXsjpjxR0tXHf0gXl/1X3jl1r\n"
    "gQrZ7w4V5AJQfWmtMfOg9yQ3HpgrQoWbvIfSQqqG9ylgNDwwqqasKygPbWOap2Lg\n"
    "bs7IUPUCgYAof3hkkYeNJg2YpIStWDhun6tMoh3bZaoK/9wM+CGzBAHBOPjY8JvA\n"
    "iegtYEMN3IQ5F0i93YpIxLIr0GY8zu4UjamipNs85PLAFKHgcwk5FJuwbJc08qbK\n"
    "DoL7VGw0Qie5XKqicpoYMq981k2kcvFBqcG1UNhzflus3687odSQ4A==\n"
    "-----END RSA PRIVATE KEY-----\n";

static const gchar* dtlsCertificate =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDtTCCAp2gAwIBAgIJAPp7zXaiL2IeMA0GCSqGSIb3DQEBCwUAMEUxCzAJBgNV\n"
    "BAYTAkFVMRMwEQYDVQQIEwpTb21lLVN0YXRlMSEwHwYDVQQKExhJbnRlcm5ldCBX\n"
    "aWRnaXRzIFB0eSBMdGQwHhcNMTUwNjI2MDU1MjQ4WhcNMTYwNjI1MDU1MjQ4WjBF\n"
    "MQswCQYDVQQGEwJBVTETMBEGA1UECBMKU29tZS1TdGF0ZTEhMB8GA1UEChMYSW50\n"
    "ZXJuZXQgV2lkZ2l0cyBQdHkgTHRkMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIB\n"
    "CgKCAQEAx4npCHPrreO5YSUYrZXjN/JTiLaaqbq9h8sSaF5lke64r8JOapGWXaLb\n"
    "McUXfLkFtL4hydo0fyki8iN+oPuMJjEu0c/DX/N1N57wjnHFNY9mXJNxhL4Ev64P\n"
    "KxBrjTx/3CKMR6u8ssi0lIcnRjw9/bMKvedWl1S9wEbJeszd1rhLwAwD5euqYkzL\n"
    "UtlRthkines86E1zHKVofr0NED22RkL+WgC2q1Y6Ic09wjAqCK+4QVsX4XqFmOqY\n"
    "T/I1sb+JPZaIFOaowWWXaBxdKXQ4WbLqm25wA1ilyy1tMUsbnl8SxouqF0dj5Mds\n"
    "KtNBckueV3Ex38fgCyz7EGhyMtqqzQIDAQABo4GnMIGkMB0GA1UdDgQWBBSZi+/v\n"
    "10ihdTH3w3S5rOpPOaj4MDB1BgNVHSMEbjBsgBSZi+/v10ihdTH3w3S5rOpPOaj4\n"
    "MKFJpEcwRTELMAkGA1UEBhMCQVUxEzARBgNVBAgTClNvbWUtU3RhdGUxITAfBgNV\n"
    "BAoTGEludGVybmV0IFdpZGdpdHMgUHR5IEx0ZIIJAPp7zXaiL2IeMAwGA1UdEwQF\n"
    "MAMBAf8wDQYJKoZIhvcNAQELBQADggEBAD+T4YTxIMOirPMP7pol1hRO6NANX7UF\n"
    "Crx3pbGYe3B5oer1HczKgRAWGBWVwgkH+zN4cJGsHWkCToh2n8JKIrUYb3qem7ET\n"
    "KBMEFMKaSkKN6VKzAz8pp1zt2gm/cPSN+uJhI7a763sLqR9apWXBuKfkWD4Z4YOi\n"
    "3s/8+E/aTrAPwDAt3ipOewqs1zCCIQSXNCZ7D1cCQGuEt6u7nlFotQx/28gkrz+2\n"
    "TuDoAwaEuZfpaZKqHHIqNBFExwUDc0KdAFogniq2YaRwcSq9/xUaReCKLG5XoopT\n"
    "Rc3p8yIIO7cpNt1VW1J3hwsZpc61CCQu4dlTIfqKJ8zkJFa0RjVvz28=\n"
    "-----END CERTIFICATE-----\n";

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

static gboolean generateDtlsCertificate(gpointer data)
{
    MediaEndpointOwr* mediaEndpoint = (MediaEndpointOwr*) data;

    // FIXME: use hard coded cert and key until the crypto utils have landed
    // in OpenWebRTC
    mediaEndpoint->dispatchDtlsCertificate(String(dtlsCertificate));
    return false;
}

void MediaEndpointOwr::getDtlsCertificate()
{
    g_idle_add(generateDtlsCertificate, this);
}

Vector<RefPtr<MediaPayload>> MediaEndpointOwr::getDefaultAudioPayloads()
{
    Vector<RefPtr<MediaPayload>> payloads;

    RefPtr<MediaPayload> payload = MediaPayload::create();
    payload->setType(111);
    payload->setEncodingName("OPUS");
    payload->setClockRate(48000);
    payload->setChannels(2);
    payloads.append(payload);

    payload = MediaPayload::create();
    payload->setType(8);
    payload->setEncodingName("PCMA");
    payload->setClockRate(8000);
    payload->setChannels(1);
    payloads.append(payload);

    payload = MediaPayload::create();
    payload->setType(0);
    payload->setEncodingName("PCMU");
    payload->setClockRate(8000);
    payload->setChannels(1);
    payloads.append(payload);

    return payloads;
}

Vector<RefPtr<MediaPayload>> MediaEndpointOwr::getDefaultVideoPayloads()
{
    Vector<RefPtr<MediaPayload>> payloads;

    // RefPtr<MediaPayload> payload = MediaPayload::create();
    // payload->setType(103);
    // payload->setEncodingName("H264");
    // payload->setClockRate(90000);
    // payload->setCcmfir(true);
    // payload->setNackpli(true);
    // payload->addParameter("packetizationMode", 1);
    // payloads.append(payload);

    RefPtr<MediaPayload> payload = MediaPayload::create();
    payload->setType(100);
    payload->setEncodingName("VP8");
    payload->setClockRate(90000);
    payload->setCcmfir(true);
    payload->setNackpli(true);
    payload->setNack(true);
    payloads.append(payload);

    payload = MediaPayload::create();
    payload->setType(120);
    payload->setEncodingName("RTX");
    payload->setClockRate(90000);
    payload->addParameter("apt", 100);
    payload->addParameter("rtxTime", 200);
    payloads.append(payload);

    return payloads;
}

MediaEndpointPrepareResult MediaEndpointOwr::prepareToReceive(MediaEndpointConfiguration* configuration, bool isInitiator)
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

    return MediaEndpointPrepareResult::Success;
}

static RefPtr<MediaPayload> findRtxPayload(Vector<RefPtr<MediaPayload>> payloads, unsigned apt)
{
    for (auto& payload : payloads) {
        if (payload->encodingName().upper() == "RTX" && payload->parameters().contains("apt")
            && (payload->parameters().get("apt") == apt))
            return payload;
    }
    return nullptr;
}

MediaEndpointPrepareResult MediaEndpointOwr::prepareToSend(MediaEndpointConfiguration* configuration, bool isInitiator)
{
    Vector<SessionConfig> sessionConfigs;
    for (unsigned i = m_sessions.size(); i < configuration->mediaDescriptions().size(); ++i) {
        SessionConfig config;
        config.type = SessionTypeMedia;
        config.isDtlsClient = configuration->mediaDescriptions()[i]->dtlsSetup() != "active";
        sessionConfigs.append(WTF::move(config));
    }

    ensureTransportAgentAndSessions(isInitiator, sessionConfigs);

    for (unsigned i = 0; i < m_sessions.size(); ++i) {
        if (i >= configuration->mediaDescriptions().size())
            printf("prepareToSend: BAD missing configuration element for %d\n", i);

        OwrSession* session = m_sessions[i];
        PeerMediaDescription& mdesc = *configuration->mediaDescriptions()[i];

        if (mdesc.type() == "audio" || mdesc.type() == "video")
            g_object_set(session, "rtcp-mux", mdesc.rtcpMux(), nullptr);

        if (mdesc.iceCandidates().size()) {
            for (auto& candidate : mdesc.iceCandidates())
                internalAddRemoteCandidate(session, *candidate, mdesc.iceUfrag(), mdesc.icePassword());
        }

        if (i < m_numberOfSendPreparedSessions)
            continue;

        if (!mdesc.source())
            continue;

        MediaPayload* payload = nullptr;
        for (auto& p : mdesc.payloads()) {
            if (p->encodingName().upper() != "RTX") {
                payload = p.get();
                break;
            }
        }

        if (!payload) {
            printf("prepareToSend: no payloads\n");
            return MediaEndpointPrepareResult::Failed;
        }

        RefPtr<MediaPayload> rtxPayload = findRtxPayload(mdesc.payloads(), payload->type());
        RealtimeMediaSourceOwr* source = static_cast<RealtimeMediaSourceOwr*>(mdesc.source());

        ASSERT(codecTypes.find(payload->encodingName().upper()) != notFound);
        OwrCodecType codecType = static_cast<OwrCodecType>(codecTypes.find(payload->encodingName().upper()));

        OwrPayload* sendPayload;
        if (mdesc.type() == "audio")
            sendPayload = owr_audio_payload_new(codecType, payload->type(), payload->clockRate(), payload->channels());
        else {
            sendPayload = owr_video_payload_new(codecType, payload->type(), payload->clockRate(), payload->ccmfir(), payload->nackpli());
            g_object_set(sendPayload, "rtx-payload-type", rtxPayload ? rtxPayload->type() : -1,
                "rtx-time", rtxPayload && rtxPayload->parameters().contains("rtxTime") ? rtxPayload->parameters().get("rtxTime") : 0, nullptr);
        }

        owr_media_session_set_send_payload(OWR_MEDIA_SESSION(session), sendPayload);
        owr_media_session_set_send_source(OWR_MEDIA_SESSION(session), source->mediaSource());

        m_numberOfSendPreparedSessions = i + 1;
    }

    return MediaEndpointPrepareResult::Success;
}

void MediaEndpointOwr::addRemoteCandidate(IceCandidate& candidate, unsigned mdescIndex, const String& ufrag, const String& password)
{
    internalAddRemoteCandidate(m_sessions[mdescIndex], candidate, ufrag, password);
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

void MediaEndpointOwr::dispatchNewIceCandidate(unsigned sessionIndex, RefPtr<IceCandidate>&& iceCandidate)
{
    m_client->gotIceCandidate(sessionIndex, WTF::move(iceCandidate));
}

void MediaEndpointOwr::dispatchGatheringDone(unsigned sessionIndex)
{
    m_client->doneGatheringCandidates(sessionIndex);
}

void MediaEndpointOwr::dispatchDtlsCertificate(const String& certificate)
{
    m_client->gotDtlsCertificate(certificate);
}

void MediaEndpointOwr::dispatchRemoteSource(unsigned sessionIndex, RefPtr<RealtimeMediaSource>&& source)
{
    m_client->gotRemoteSource(sessionIndex, WTF::move(source));
}

void MediaEndpointOwr::prepareSession(OwrSession* session, PeerMediaDescription* mediaDescription)
{
    g_object_set_data_full(G_OBJECT(session), "ice-ufrag", g_strdup(mediaDescription->iceUfrag().ascii().data()), g_free);
    g_object_set_data_full(G_OBJECT(session), "ice-password", g_strdup(mediaDescription->icePassword().ascii().data()), g_free);

    g_signal_connect(session, "on-new-candidate", G_CALLBACK(gotCandidate), this);
    g_signal_connect(session, "on-candidate-gathering-done", G_CALLBACK(candidateGatheringDone), this);
}

void MediaEndpointOwr::prepareMediaSession(OwrMediaSession* mediaSession, PeerMediaDescription* mediaDescription, bool isInitiator)
{
    prepareSession(OWR_SESSION(mediaSession), mediaDescription);

    bool useRtpMux = !isInitiator && mediaDescription->rtcpMux();
    g_object_set(mediaSession, "rtcp-mux", useRtpMux,
        "cname", mediaDescription->cname().ascii().data(),
        "send-ssrc", mediaDescription->ssrcs()[0],
        nullptr);

    g_signal_connect(mediaSession, "on-incoming-source", G_CALLBACK(gotIncomingSource), this);

    for (auto& payload : mediaDescription->payloads()) {
        if (payload->encodingName().upper() == "RTX")
            continue;

        RefPtr<MediaPayload> rtxPayload = findRtxPayload(mediaDescription->payloads(), payload->type());

        ASSERT(codecTypes.find(payload->encodingName()) != notFound);
        OwrCodecType codecType = static_cast<OwrCodecType>(codecTypes.find(payload->encodingName()));

        OwrPayload* receivePayload;
        if (mediaDescription->type() == "audio")
            receivePayload = owr_audio_payload_new(codecType, payload->type(), payload->clockRate(), payload->channels());
        else {
            receivePayload = owr_video_payload_new(codecType, payload->type(), payload->clockRate(), payload->ccmfir(), payload->nackpli());
            g_object_set(receivePayload, "rtx-payload-type", rtxPayload ? rtxPayload->type() : -1,
                "rtx-time", rtxPayload && rtxPayload->parameters().contains("rtxTime") ? rtxPayload->parameters().get("rtxTime") : 0, nullptr);
        }

        owr_media_session_add_receive_payload(mediaSession, receivePayload);
    }
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

    for (auto& config : sessionConfigs) {
        OwrSession* session = OWR_SESSION(owr_media_session_new(config.isDtlsClient));
        g_object_set(session, "dtls-certificate", dtlsCertificate,
            "dtls-key", dtlsKey,
            nullptr);

        m_sessions.append(session);
    }
}

void MediaEndpointOwr::internalAddRemoteCandidate(OwrSession* session, IceCandidate& candidate, const String& ufrag, const String& password)
{
    gboolean rtcpMux;
    g_object_get(session, "rtcp-mux", &rtcpMux, nullptr);

    if (rtcpMux && candidate.componentId() == OWR_COMPONENT_TYPE_RTCP)
        return;

    ASSERT(candidateTypes.find(candidate.type()) != notFound);
    printf("ASSERT: %d\n", (candidateTypes.find(candidate.type()) != notFound));

    OwrCandidateType candidateType = static_cast<OwrCandidateType>(candidateTypes.find(candidate.type()));
    OwrComponentType componentId = static_cast<OwrComponentType>(candidate.componentId());
    OwrTransportType transportType;

    if (candidate.transport().upper() == "UDP")
        transportType = OWR_TRANSPORT_TYPE_UDP;
    else {
        ASSERT(candidateTcpTypes.find(candidate.tcpType()) != notFound);
        printf("ASSERT: %d\n", (candidateTcpTypes.find(candidate.tcpType()) != notFound));
        transportType = static_cast<OwrTransportType>(candidateTcpTypes.find(candidate.tcpType()));
    }

    OwrCandidate* owrCandidate = owr_candidate_new(candidateType, componentId);
    g_object_set(owrCandidate, "transport-type", transportType,
        "address", candidate.address().ascii().data(),
        "port", candidate.port(),
        "base-address", candidate.relatedAddress().ascii().data(),
        "base-port", candidate.relatedPort(),
        "priority", candidate.priority(),
        "foundation", candidate.foundation().ascii().data(),
        "ufrag", ufrag.ascii().data(),
        "password", password.ascii().data(),
        nullptr);

    owr_session_add_remote_candidate(session, owrCandidate);
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

    g_object_get(candidate, "type", &candidateType,
        "foundation", &foundation,
        "component-type", &componentId,
        "transport-type", &transportType,
        "priority", &priority,
        "address", &address,
        "port", &port,
        "base-address", &relatedAddress,
        "base-port", &relatedPort,
        nullptr);

    ASSERT(candidateType >= 0 && candidateType < candidateTypes.size());
    ASSERT(transportType >= 0 && transportType < candidateTcpTypes.size());

    RefPtr<IceCandidate> iceCandidate = IceCandidate::create();
    iceCandidate->setType(candidateTypes[candidateType]);
    iceCandidate->setFoundation(foundation);
    iceCandidate->setComponentId(componentId);
    iceCandidate->setPriority(priority);
    iceCandidate->setAddress(address);
    iceCandidate->setPort(port ? port : 9);

    if (transportType == OWR_TRANSPORT_TYPE_UDP)
        iceCandidate->setTransport("UDP");
    else {
        iceCandidate->setTransport("TCP");
        iceCandidate->setTcpType(candidateTcpTypes[transportType]);
    }

    if (candidateType != OWR_CANDIDATE_TYPE_HOST) {
        iceCandidate->setRelatedAddress(relatedAddress);
        iceCandidate->setRelatedPort(relatedPort ? relatedPort : 9);
    }

    g_object_set(G_OBJECT(candidate), "ufrag", g_object_get_data(G_OBJECT(session), "ice-ufrag"),
        "password", g_object_get_data(G_OBJECT(session), "ice-password"),
        nullptr);

    mediaEndpoint->dispatchNewIceCandidate(mediaEndpoint->sessionIndex(session), WTF::move(iceCandidate));

    g_free(foundation);
    g_free(address);
    g_free(relatedAddress);
}

static void candidateGatheringDone(OwrSession* session, MediaEndpointOwr* mediaEndpoint)
{
    mediaEndpoint->dispatchGatheringDone(mediaEndpoint->sessionIndex(session));
}

static void gotIncomingSource(OwrMediaSession* mediaSession, OwrMediaSource* source, MediaEndpointOwr* mediaEndpoint)
{
    String name;
    String id("not used");
    OwrMediaType mediaType;

    g_object_get(source, "media-type", &mediaType, nullptr);

    RealtimeMediaSource::Type sourceType;
    if (mediaType == OWR_MEDIA_TYPE_AUDIO) {
        sourceType = RealtimeMediaSource::Audio;
        name = "remote audio";
    }
    else if (mediaType == OWR_MEDIA_TYPE_VIDEO) {
        sourceType = RealtimeMediaSource::Video;
        name = "remote video";
    }
    else
        ASSERT_NOT_REACHED();

    RefPtr<RealtimeMediaSourceOwr> mediaSource = adoptRef(new RealtimeMediaSourceOwr(source, id, sourceType, name));
    mediaEndpoint->dispatchRemoteSource(mediaEndpoint->sessionIndex(OWR_SESSION(mediaSession)), WTF::move(mediaSource));
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
