/*
 * Copyright (C) 2015, 2016 Ericsson AB. All rights reserved.
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

// @conditional=ENABLE(WEB_RTC)

function createOffer()
{
    "use strict";

    var peerConnection = this;

    return @callbacksAndDictionaryOverload(arguments, "createOffer", function (options) {
        // Promise mode
        return @enqueueOperation(peerConnection, function () {
            return peerConnection.@queuedCreateOffer(options);
        });
    }, function (successCallback, errorCallback, options) {
        // Legacy callbacks mode
        @enqueueOperation(peerConnection, function () {
            return peerConnection.@queuedCreateOffer(options).then(successCallback, errorCallback);
        });

        return @Promise.@resolve(@undefined);
    });
}

function createAnswer()
{
    "use strict";

    var peerConnection = this;

    return @callbacksAndDictionaryOverload(arguments, "createAnswer", function (options) {
        // Promise mode
        return @enqueueOperation(peerConnection, function () {
            return peerConnection.@queuedCreateAnswer(options);
        });
    }, function (successCallback, errorCallback, options) {
        // Legacy callbacks mode
        @enqueueOperation(peerConnection, function () {
            return peerConnection.@queuedCreateAnswer(options).then(successCallback, errorCallback);
        });

        return @Promise.@resolve(@undefined);
    });
}

function addTrack()
{
    "use strict";

    return this.@privateAddTrack.@apply(this, arguments);
}

function removeTrack()
{
    "use strict";

    return this.@privateRemoveTrack.@apply(this, arguments);
}

function addStream()
{
    "use strict";

    if (arguments.length < 1)
        throw new @TypeError("Not enough arguments");

    var stream = arguments[0];
    if (!(stream instanceof @MediaStream))
        throw new @TypeError("Argument 1 ('stream') to RTCPeerConnection.addStream must be an instance of MediaStream");

    if (this.@localStreams) {
        for (let i = 0; i < this.@localStreams.length; ++i) {
            if (this.@localStreams[i].id === stream.id)
                return
        }
    } else
        this.@localStreams = [];

    this.@localStreams.push(stream);

    stream.getTracks().forEach(track => this.@privateAddTrack(track, stream));
}

function removeStream()
{
    "use strict";

    if (arguments.length < 1)
        throw new @TypeError("Not enough arguments");

    var stream = arguments[0];
    if (!(stream instanceof @MediaStream))
        throw new @TypeError("Argument 1 ('stream') to RTCPeerConnection.removeStream must be an instance of MediaStream");

    if (!this.@localStreams)
        return;

    const senders = this.getSenders();
    for (let i = 0; i < this.@localStreams.length; ++i) {
        if (this.@localStreams[i].id === stream.id) {
            this.@localStreams[i].getTracks().forEach(track => {
                // Find track's sender and call removeTrack with it.
                for (let j = 0; j < senders.length; ++j) {
                    let sender = senders[j];
                    if (sender.track && sender.track.id === track.id) {
                        this.@privateRemoveTrack(sender);
                        break;
                    }
                }
            });

            this.@localStreams.splice(i, 1);
            break;
        }
    }
}

function getLocalStreams()
{
    "use strict";

    if (!this.@localStreams)
        return [];

    return this.@localStreams.slice();
}

function getRemoteStreams()
{
    "use strict";

    return this.@privateGetRemoteStreams();
}

function getStreamById()
{
    "use strict";

    if (arguments.length < 1)
        throw new @TypeError("Not enough arguments");

    const streamId = String(arguments[0]);

    if (this.@localStreams) {
        for (let i = 0; i < this.@localStreams.length; ++i) {
            if (this.@localStreams[i].id === streamId)
                return this.@localStreams[i];
        }
    }

    const remoteStreams = this.@privateGetRemoteStreams();
    for (let i = 0; i < remoteStreams.length; ++i) {
        if (remoteStreams[i].id === streamId)
            return remoteStreams[i];
    }

    return null;
}

function setLocalDescription()
{
    "use strict";

    var peerConnection = this;

    return @objectAndCallbacksOverload(arguments, "setLocalDescription", @RTCSessionDescription, {}, function (description) {
        // Promise mode
        return @enqueueOperation(peerConnection, function () {
            return peerConnection.@queuedSetLocalDescription(description);
        });
    }, function (description, successCallback, errorCallback) {
        // Legacy callbacks mode
        @enqueueOperation(peerConnection, function () {
            return peerConnection.@queuedSetLocalDescription(description).then(successCallback, errorCallback);
        });

        return @Promise.@resolve(@undefined);
    });
}

function setRemoteDescription()
{
    "use strict";

    var peerConnection = this;

    return @objectAndCallbacksOverload(arguments, "setRemoteDescription", @RTCSessionDescription, {}, function (description) {
        // Promise mode
        return @enqueueOperation(peerConnection, function () {
            return peerConnection.@queuedSetRemoteDescription(description);
        });
    }, function (description, successCallback, errorCallback) {
        // Legacy callbacks mode
        @enqueueOperation(peerConnection, function () {
            return peerConnection.@queuedSetRemoteDescription(description).then(successCallback, errorCallback);
        });

        return @Promise.@resolve(@undefined);
    });
}

function addIceCandidate()
{
    "use strict";

    var peerConnection = this;

    return @objectAndCallbacksOverload(arguments, "addIceCandidate", @RTCIceCandidate, {}, function (candidate) {
        // Promise mode
        return @enqueueOperation(peerConnection, function () {
            return peerConnection.@queuedAddIceCandidate(candidate);
        });
    }, function (candidate, successCallback, errorCallback) {
        // Legacy callbacks mode
        @enqueueOperation(peerConnection, function () {
            return peerConnection.@queuedAddIceCandidate(candidate).then(successCallback, errorCallback);
        });

        return @Promise.@resolve(@undefined);
    });
}

function getStats()
{
    "use strict";

    var peerConnection = this;

    var objectOptions = { "optionalAndNullable": true };
    return @objectAndCallbacksOverload(arguments, "getStats", @MediaStreamTrack, objectOptions, function (selector) {
        // Promise mode
        return peerConnection.@privateGetStats(selector);
    }, function (selector, successCallback, errorCallback) {
        // Legacy callbacks mode
        peerConnection.@privateGetStats(selector).then(successCallback, errorCallback);

        return @Promise.@resolve(@undefined);
    });
}
