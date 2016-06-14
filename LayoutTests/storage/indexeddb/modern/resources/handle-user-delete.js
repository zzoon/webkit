description("Tests that expected errors come back from user delete.");

indexedDBTest(prepareDatabase);

var requestErrorCount = 0;
var databaseError = false;

function done()
{
    if (requestErrorCount == 2)
        log("[PASS] Both requests hit a failure condition (Received onerror or failed to start a new request because the transaction was aborted)");
    else
        log("[FAIL] " + requestErrorCount + " request(s) hit a failure condition.");

    if (databaseError)
        log("[PASS] Database received correct error.");
    else
        log("[FAIL] Database did not receive correct error.");
        
    finishJSTest();
}

function log(message)
{
    debug(message);
}

function maybeFinish()
{
    if (requestErrorCount == 2 && databaseError)
        done();
}

function prepareDatabase(event)
{
    log("Initial upgrade needed: Old version - " + event.oldVersion + " New version - " + event.newVersion);

    var versionTransaction = event.target.transaction;
    var database = event.target.result;
    var objectStore = database.createObjectStore("TestObjectStore");
    objectStore.put("bar", "foo");
    
    database.onerror = function(event) {
        databaseError = true;
        maybeFinish();
    }

    var hasClearedDatabases = false;
    var spinGet = function() { 
        try {
            var req = objectStore.get("foo");
        } catch(e) {
            ++requestErrorCount;
            maybeFinish();
            return;   
        }
        req.onsuccess = function() {
            spinGet();
            if (!hasClearedDatabases) {
                if (window.testRunner) {
                    setTimeout("testRunner.clearAllDatabases();", 0);
                    log("Requested clearAllDatabases");
                }
                hasClearedDatabases = true;
            }
        }
        req.onerror = function(event) {
            ++requestErrorCount;
            event.stopImmediatePropagation();
            maybeFinish();
        }
    }
    // Start up two get cycles so there will always be at least one request to cancel when the database is deleted.
    spinGet();
    spinGet();
    
    log("Started two spinning requests")

    versionTransaction.onabort = function(event) {
        log("Initial upgrade versionchange transaction aborted: " + versionTransaction.error);
    }

    versionTransaction.oncomplete = function() {
        log("Initial upgrade versionchange transaction unexpected complete");
        done();
    }

    versionTransaction.onerror = function(event) {
        log("Initial upgrade versionchange transaction unexpected error: " + event.type + " " + versionTransaction.error.name + ", " + versionTransaction.error.message);
        done();
    }
}
