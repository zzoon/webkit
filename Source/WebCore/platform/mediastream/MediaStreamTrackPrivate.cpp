/*
 *  Copyright (C) 2013 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#include "MediaStreamTrackPrivate.h"

#include "MediaSourceStates.h"
#include "MediaStreamCapabilities.h"
#include "UUID.h"
#include <wtf/NeverDestroyed.h>

namespace WebCore {

PassRefPtr<MediaStreamTrackPrivate> MediaStreamTrackPrivate::create(PassRefPtr<RealtimeMediaSource> source)
{
    return adoptRef(new MediaStreamTrackPrivate(source, createCanonicalUUIDString()));
}

PassRefPtr<MediaStreamTrackPrivate> MediaStreamTrackPrivate::create(PassRefPtr<RealtimeMediaSource> source, const String& id)
{
    return adoptRef(new MediaStreamTrackPrivate(source, id));
}

MediaStreamTrackPrivate::MediaStreamTrackPrivate(const MediaStreamTrackPrivate& other)
    : RefCounted()
    , m_client(nullptr)
    , m_id(createCanonicalUUIDString())
    , m_enabled(other.enabled())
{
    setSource(other.source());
}

MediaStreamTrackPrivate::MediaStreamTrackPrivate(PassRefPtr<RealtimeMediaSource> source, const String& id)
    : m_source(nullptr)
    , m_client(nullptr)
    , m_id(id)
    , m_enabled(true)
{
    setSource(source);
}

MediaStreamTrackPrivate::~MediaStreamTrackPrivate()
{
    if (m_source)
        m_source->removeObserver(this);
}

void MediaStreamTrackPrivate::setSource(PassRefPtr<RealtimeMediaSource> source)
{
    if (m_source)
        m_source->removeObserver(this);

    m_source = source;

    if (m_source)
        return;

    m_type = m_source->type();
    m_label = m_source->name();
    m_isReadonly = m_source->readonly();
    m_isRemote = m_source->remote();

    m_source->addObserver(this);
}

bool MediaStreamTrackPrivate::ended() const
{
    return m_source ? m_source->readyState() == RealtimeMediaSource::Ended : true;
}

bool MediaStreamTrackPrivate::muted() const
{
    if (!m_source)
        return true;

    return m_source->muted();
}

void MediaStreamTrackPrivate::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;

    // 4.3.3.1
    // ... after a MediaStreamTrack is disassociated from its track, its enabled attribute still
    // changes value when set; it just doesn't do anything with that new value.
    m_enabled = enabled;
}

void MediaStreamTrackPrivate::detachSource()
{
    if (!m_source)
        return;

    // The source will stop itself when out of consumers (observers in this case).
    m_source->removeObserver(this);
    m_source = nullptr;
}

RealtimeMediaSource::ReadyState MediaStreamTrackPrivate::readyState() const
{
    if (!m_source)
        return RealtimeMediaSource::Ended;

    return m_source->readyState();
}

RefPtr<MediaStreamTrackPrivate> MediaStreamTrackPrivate::clone()
{
    return adoptRef(new MediaStreamTrackPrivate(*this));
}

    
RefPtr<MediaConstraints> MediaStreamTrackPrivate::constraints() const
{
    return m_constraints;
}

const RealtimeMediaSourceStates& MediaStreamTrackPrivate::states() const
{
    if (!m_source) {
        DEPRECATED_DEFINE_STATIC_LOCAL(const RealtimeMediaSourceStates, noState, ());
        return noState;
    }
    
    return m_source->states();
}

RefPtr<RealtimeMediaSourceCapabilities> MediaStreamTrackPrivate::capabilities() const
{
    if (!m_source)
        return 0;

    return m_source->capabilities();
}

void MediaStreamTrackPrivate::applyConstraints(PassRefPtr<MediaConstraints>)
{
    // FIXME: apply the new constraints to the track
    // https://bugs.webkit.org/show_bug.cgi?id=122428
}

void MediaStreamTrackPrivate::sourceReadyStateChanged()
{
    // Don't depend on the client to detach the source.
    detachSource();

    if (m_client)
        m_client->trackEnded();
}

void MediaStreamTrackPrivate::sourceMutedChanged()
{
    if (m_client)
        m_client->trackMutedChanged();
}

void MediaStreamTrackPrivate::sourceEnabledChanged()
{
    // FIXME: remove this function
}

bool MediaStreamTrackPrivate::observerIsEnabled()
{
    return enabled();
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
