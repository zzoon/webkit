/*
 * Copyright (C) 2015, 2016 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "ActiveDOMCallback.h"
#include "Microtasks.h"
#include <wtf/NoncopyableFunction.h>

namespace WebCore {

class ActiveDOMCallbackMicrotask final : public Microtask, public ActiveDOMCallback {
    WTF_MAKE_FAST_ALLOCATED;
public:
    WEBCORE_EXPORT ActiveDOMCallbackMicrotask(MicrotaskQueue&, ScriptExecutionContext&, NoncopyableFunction<void()>&&);
    WEBCORE_EXPORT virtual ~ActiveDOMCallbackMicrotask();

    Result run() override;

private:
    void contextDestroyed() override;

    // FIXME: It should not be necessary to have the queue as a member. Instead, it should
    // be accessed via the ScriptExecutionContext, which should hold a reference to the relevent
    // queue.
    MicrotaskQueue& m_queue;
    NoncopyableFunction<void()> m_task;
};

} // namespace WebCore
