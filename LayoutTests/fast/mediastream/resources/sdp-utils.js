(function (globalObject) {
    // Variable fields (e.g. generated ids) are verified and replaced with predictable
    // lables. The result can be compared with a predefined expected output.
    function printComparableSessionDescription(sessionDescription, mdescVariables) {
        debug("=== RTCSessionDescription ===");
        debug("type: " + sessionDescription.type + ", sdp:");

        var sdp = sessionDescription.sdp;

        var regexp = {
            "oline": "^o=(-) ([\\d]+) .*$",
            "msidsemantic": "^a=msid-semantic: *WMS .*$",
            "mline": "^m=.*$",
            "cname": "^a=ssrc:(\\d+) cname:([\\w+/\\-@\\.\\{\\}]+).*$",
            "mid": "^a=mid:([!#$%&'*+-.\\w]*).*$",
            "msid": "^a=(ssrc:\\d+ )?msid:([\\w+/\\-=]+) +([\\w+/\\-=]+).*$",
            "iceufrag": "^a=ice-ufrag:([\\w+/]*).*$",
            "icepwd": "^a=ice-pwd:([\\w+/]*=*).*$",
        };

        var mdescIndex = -1;
        sdp.split("\r\n").forEach(function (line) {
            if (match(line, regexp.mline)) {
                // Media block ("header" line)
                mdescIndex++;
            } else if (mdescIndex < 0) {
                // Session block
                var oline;

                if (oline = match(line, regexp.oline))
                    line = line.replace(oline[2], verified("session-id"));
                else if (match(line, regexp.msidsemantic)) {
                    mdescVariables.forEach(function (variables) {
                        line = line.replace(variables.streamId, verified("media-stream-id"));
                    });
                }
            } else {
                // Media block (content lines)
                var cname;
                var mid;
                var msid;
                var iceufrag;
                var icepwd;

                if (cname = match(line, regexp.cname)) {
                    line = line.replace(cname[1], verified("ssrc"));
                    line = line.replace(cname[2], verified("cname"));
                } else if (mid = match(line, regexp.mid))
                    line = line.replace(mid[1], verified("mid"))
                else if (msid = match(line, regexp.msid)) {
                    if (msid[1])
                        line = line.replace(msid[1], verified("ssrc"));

                    var variables = mdescVariables[mdescIndex];

                    var mediaStreamId = msid[2];
                    var streamIdVerified = verified("media-stream-id", mediaStreamId != variables.streamId);
                    line = line.replace(mediaStreamId, streamIdVerified);

                    var mediaStreamTrackId = msid[3];
                    var trackIdVerified = verified("media-stream-track-id", mediaStreamTrackId != variables.trackId);
                    line = line.replace(mediaStreamTrackId, trackIdVerified);

                } else if (iceufrag = match(line, regexp.iceufrag))
                    line = line.replace(iceufrag[1], verified("ice-ufrag"));
                else if (icepwd = match(line, regexp.icepwd))
                    line = line.replace(icepwd[1], verified("ice-password"));
            }

            if (line)
                debug(line);
        });

        debug("===");
        debug("");
    }

    function verified(name, isFailure) {
        return "{" + name + ":" + (isFailure ? "FAILED" : "OK") + "}";
    }

    function match(data, pattern) {
        return data.match(new RegExp(pattern));
    }

    // Exposed function(s)
    globalObject.printComparableSessionDescription = printComparableSessionDescription;

})(self);
