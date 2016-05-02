function promiseShouldResolve(expr) {
    return new Promise(function (done) {
        var p;
        try {
            p = eval(expr);
        } catch (e) {
            testFailed("evaluating " + expr + " threw exception " + e);
            done();
        }

        if (!(p instanceof Promise)) {
            testFailed(expr + " does not evaluate to a promise.");
            done();
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
        var p;
        try {
            p = eval(expr);
        } catch (e) {
            testFailed("evaluating " + expr + " threw exception " + e);
            done();
        }

        if (!(p instanceof Promise)) {
            testFailed(expr + " does not evaluate to a promise.");
            done();
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
