'use strict';

let assert = require('assert');

require('./v3-models.js');

let BuildbotSyncer = require('./buildbot-syncer').BuildbotSyncer;

class BuildbotTriggerable {
    constructor(config, remote, buildbotRemote, slaveInfo, logger)
    {
        this._name = config.triggerableName;
        assert(typeof(this._name) == 'string', 'triggerableName must be specified');

        this._lookbackCount = config.lookbackCount;
        assert(typeof(this._lookbackCount) == 'number' && this._lookbackCount > 0, 'lookbackCount must be a number greater than 0');

        this._remote = remote;

        this._slaveInfo = slaveInfo;
        assert(typeof(slaveInfo.name) == 'string', 'slave name must be specified');
        assert(typeof(slaveInfo.password) == 'string', 'slave password must be specified');

        this._syncers = BuildbotSyncer._loadConfig(buildbotRemote, config);
        this._logger = logger || {log: function () { }, error: function () { }};
    }

    name() { return this._name; }

    updateTriggerable()
    {
        const map = new Map;
        for (const syncer of this._syncers) {
            for (const config of syncer.testConfigurations()) {
                const entry = {test: config.test.id(), platform: config.platform.id()};
                map.set(entry.test + '-' + entry.platform, entry);
            }
        }
        return this._remote.postJSON(`/api/update-triggerable/`, {
            'slaveName': this._slaveInfo.name,
            'slavePassword': this._slaveInfo.password,
            'triggerable': this._name,
            'configurations': Array.from(map.values())});
    }

    syncOnce()
    {
        let syncerList = this._syncers;
        let buildReqeustsByGroup = new Map;

        let self = this;
        this._logger.log(`Fetching build requests for ${this._name}...`);
        return BuildRequest.fetchForTriggerable(this._name).then(function (buildRequests) {
            self._validateRequests(buildRequests);
            buildReqeustsByGroup = BuildbotTriggerable._testGroupMapForBuildRequests(buildRequests);
            return self._pullBuildbotOnAllSyncers(buildReqeustsByGroup);
        }).then(function (updates) {
            self._logger.log('Scheduling builds');
            let promistList = [];
            let testGroupList = Array.from(buildReqeustsByGroup.values()).sort(function (a, b) { return a.groupOrder - b.groupOrder; });
            for (let group of testGroupList) {
                let promise = self._scheduleNextRequestInGroupIfSlaveIsAvailable(group, updates);
                if (promise)
                    promistList.push(promise);
            }
            return Promise.all(promistList);
        }).then(function () {
            // Pull all buildbots for the second time since the previous step may have scheduled more builds.
            return self._pullBuildbotOnAllSyncers(buildReqeustsByGroup);
        }).then(function (updates) {
            // FIXME: Add a new API that just updates the requests.
            return self._remote.postJSON(`/api/build-requests/${self._name}`, {
                'slaveName': self._slaveInfo.name,
                'slavePassword': self._slaveInfo.password,
                'buildRequestUpdates': updates});
        }).then(function (response) {
            if (response['status'] != 'OK')
                self._logger.log('Failed to update the build requests status: ' + response['status']);
        })
    }

    _validateRequests(buildRequests)
    {
        let testPlatformPairs = {};
        for (let request of buildRequests) {
            if (!this._syncers.some(function (syncer) { return syncer.matchesConfiguration(request); })) {
                let key = request.platform().id + '-' + request.test().id();
                if (!(key in testPlatformPairs))
                    this._logger.error(`No matching configuration for "${request.test().fullName()}" on "${request.platform().name()}".`);                
                testPlatformPairs[key] = true;
            }
        }
    }

    _pullBuildbotOnAllSyncers(buildReqeustsByGroup)
    {
        let updates = {};
        let associatedRequests = new Set;
        let self = this;
        return Promise.all(this._syncers.map(function (syncer) {
            return syncer.pullBuildbot(self._lookbackCount).then(function (entryList) {
                for (let entry of entryList) {
                    let request = BuildRequest.findById(entry.buildRequestId());
                    if (!request)
                        continue;
                    associatedRequests.add(request);

                    let info = buildReqeustsByGroup.get(request.testGroupId());
                    assert(!info.syncer || info.syncer == syncer);
                    info.syncer = syncer;
                    if (entry.slaveName()) {
                        assert(!info.slaveName || info.slaveName == entry.slaveName());
                        info.slaveName = entry.slaveName();
                    }

                    let newStatus = entry.buildRequestStatusIfUpdateIsNeeded(request);
                    if (newStatus) {
                        self._logger.log(`Updating the status of build request ${request.id()} from ${request.status()} to ${newStatus}`);
                        updates[entry.buildRequestId()] = {status: newStatus, url: entry.url()};
                    } else if (!request.statusUrl()) {
                        self._logger.log(`Setting the status URL of build request ${request.id()} to ${entry.url()}`);
                        updates[entry.buildRequestId()] = {status: request.status(), url: entry.url()};
                    }
                }
            });
        })).then(function () {
            for (let request of BuildRequest.all()) {
                if (request.hasStarted() && !request.hasFinished() && !associatedRequests.has(request)) {
                    self._logger.log(`Updating the status of build request ${request.id()} from ${request.status()} to failedIfNotCompleted`);
                    assert(!(request.id() in updates));
                    updates[request.id()] = {status: 'failedIfNotCompleted'};
                }
            }
        }).then(function () { return updates; });
    }

    _scheduleNextRequestInGroupIfSlaveIsAvailable(groupInfo, pendingUpdates)
    {
        let nextRequest = null;
        for (let request of groupInfo.requests) {
            if (request.isScheduled() || (request.id() in pendingUpdates && pendingUpdates[request.id()]['status'] == 'scheduled'))
                break;
            if (request.isPending() && !(request.id() in pendingUpdates)) {
                nextRequest = request;
                break;
            }
        }
        if (!nextRequest)
            return null;

        let promise;
        let syncer;
        if (!!nextRequest.order()) {
            syncer = groupInfo.syncer;
            if (!syncer)
                this._logger.error(`Could not identify the syncer for ${nextRequest.id()}.`);
            else
                promise = syncer.scheduleRequestInGroupIfAvailable(nextRequest, groupInfo.slaveName);
        }

        if (!syncer) {
            for (syncer of this._syncers) {
                let promise = syncer.scheduleRequestInGroupIfAvailable(nextRequest);
                if (promise)
                    break;
            }
        }

        if (promise) {
            let slaveName = groupInfo.slaveName ? ` on ${groupInfo.slaveName}` : '';
            this._logger.log(`Scheduling build request ${nextRequest.id()}${slaveName} in ${syncer.builderName()}`);
            return promise;
        }

        return null;
    }

    static _testGroupMapForBuildRequests(buildRequests)
    {
        let map = new Map;
        let groupOrder = 0;
        for (let request of buildRequests) {
            let groupId = request.testGroupId();
            if (!map.has(groupId)) // Don't use real TestGroup objects to avoid executing postgres query in the server
                map.set(groupId, {id: groupId, groupOrder: groupOrder++, requests: [request], syncer: null, slaveName: null});
            else
                map.get(groupId).requests.push(request);
        }
        return map;
    }
}

if (typeof module != 'undefined')
    module.exports.BuildbotTriggerable = BuildbotTriggerable;
