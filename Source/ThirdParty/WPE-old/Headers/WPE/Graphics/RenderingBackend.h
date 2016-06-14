/*
 * Copyright (C) 2015 Igalia S.L.
 * All rights reserved.
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
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WPE_Graphics_RenderingBackend_h
#define WPE_Graphics_RenderingBackend_h

#include <WPE/WPE.h>

#include <EGL/eglplatform.h>
#include <memory>
#include <tuple>

namespace WPE {

namespace Graphics {

class RenderingBackend {
public:
    using BufferExport = std::tuple<int, const uint8_t*, size_t>;

    class Surface {
    public:
        class Client {
        public:
            virtual void destroyBuffer(uint32_t) = 0;
        };

        virtual ~Surface();

        virtual EGLNativeWindowType nativeWindow() = 0;
        virtual void resize(uint32_t, uint32_t) = 0;

        virtual BufferExport lockFrontBuffer() = 0;
        virtual void releaseBuffer(uint32_t) = 0;
    };

    class OffscreenSurface {
    public:
        virtual ~OffscreenSurface();

        virtual EGLNativeWindowType nativeWindow() = 0;
    };

    static WPE_EXPORT std::unique_ptr<RenderingBackend> create();

    virtual ~RenderingBackend();

    virtual EGLNativeDisplayType nativeDisplay() = 0;
    virtual std::unique_ptr<Surface> createSurface(uint32_t, uint32_t, uint32_t, Surface::Client&) = 0;
    virtual std::unique_ptr<OffscreenSurface> createOffscreenSurface() = 0;
};

} // namespace Graphics

} // namespace WPE

#endif // WPE_Graphics_RenderingBackend_h
