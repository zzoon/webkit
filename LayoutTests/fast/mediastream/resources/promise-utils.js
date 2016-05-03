function ensurePromise(expr) {
    var p;
    try {
        p = eval(expr);
    } catch (e) {
        testFailed("evaluating " + expr + " threw exception " + e);
        return null;
    }

    if (!(p instanceof Promise)) {
        testFailed(expr + " does not evaluate to a promise.");
        return null;
    }

    return p;
}

function promiseShouldResolve(expr) {
    return new Promise(function (done) {
        var p = ensurePromise(expr);
        if (!p) {
            done();
            return;
        }

        p.then(function (value) {
            testPassed("promise " + expr + " fulfilled with " + value);
            done();
        })
        .catch(function (reason) {
            testFailed("promise " + expr + " rejected unexpectedly.");
            done();
        });
    });
}

function promiseShouldReject(expr) {
    return new Promise(function (done) {
        var p = ensurePromise(expr);
        if (!p) {
            done();
            return;
        }

        p.then(function () {
            testFailed("promise " + expr + " fulfilled unexpectedly.");
            done();
        })
        .catch(function (reason) {
            testPassed("promise " + expr + " rejected with " + reason);
            done();
        });
    });
}
