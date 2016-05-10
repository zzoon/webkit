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

// @conditional=ENABLE(WEB_RTC)
// @internal

// Operation queue as specified in section 4.3.1 (WebRTC 1.0)
function enqueueOperation(peerConnection, operation)
{
    "use strict";

    if (!peerConnection.@operations)
        peerConnection.@operations = [];

    var operations = peerConnection.@operations;

    function runNext() {
        operations.@shift();
        if (operations.length)
            operations[0]();
    };

    return new @Promise(function (resolve, reject) {
        operations.@push(function() {
            operation().then(resolve, reject).then(runNext, runNext);
        });

        if (operations.length == 1)
            operations[0]();
    });
}

function objectAndCallbacksOverload(args, functionName, objectConstructor, objectOptions, promiseMode, legacyMode)
{
    "use strict";

    var argsCount = Math.min(3, args.length);
    var objectArg = args[0];
    var objectArgOk = false;

    if (argsCount == 0 && objectOptions.optionalAndNullable) {
        objectArg = null;
        objectArgOk = true;
        argsCount = 1;
    } else {
        var hasMatchingType = objectArg instanceof objectConstructor;
        objectArgOk = objectOptions.optionalAndNullable ? (objectArg === null || typeof objectArg === "undefined" || hasMatchingType) : hasMatchingType;
    }

    if (argsCount == 1 && objectArgOk)
        return promiseMode(objectArg);

    var successCallback = args[1];
    var errorCallback = args[2];
    if (argsCount == 3 && objectArgOk
        && (successCallback == null || typeof successCallback === "function")
        && (errorCallback == null || typeof errorCallback === "function")) {
        if (typeof successCallback !== "function")
            return @Promise.@reject(new @TypeError(`Argument 2 ('successCallback') to RTCPeerConnection.${functionName} must be a Function`));

        if (typeof errorCallback !== "function")
            return @Promise.@reject(new @TypeError(`Argument 3 ('errorCallback') to RTCPeerConnection.${functionName} must be a Function`));

        return legacyMode(objectArg, successCallback, errorCallback);
    }

    if (argsCount < 1)
        return @Promise.@reject(new @TypeError("Not enough arguments"));

    return @Promise.@reject(new @TypeError("Type error"));
}

function callbacksAndDictionaryOverload(args, functionName, promiseMode, legacyMode)
{
    "use strict";

    var argsCount = Math.min(3, args.length);

    if (argsCount == 0 || argsCount == 1)
        return promiseMode(args[0]);

    var successCallback = args[0];
    var errorCallback = args[1];
    if ((argsCount == 2 || argsCount == 3)
        && (successCallback == null || typeof successCallback === "function")
        && (errorCallback == null || typeof errorCallback === "function")) {
        if (typeof successCallback !== "function")
            return @Promise.@reject(new @TypeError(`Argument 1 ('successCallback') to RTCPeerConnection.${functionName} must be a Function`));

        if (typeof errorCallback !== "function")
            return @Promise.@reject(new @TypeError(`Argument 2 ('errorCallback') to RTCPeerConnection.${functionName} must be a Function`));

        return legacyMode(successCallback, errorCallback, args[2]);
    }

    return @Promise.@reject(new @TypeError("Type error"));
}
