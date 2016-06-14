'use strict';

class CommitLog extends DataModelObject {
    constructor(id, rawData)
    {
        console.assert(parseInt(id) == id);
        super(id);
        this._repository = rawData.repository;
        console.assert(this._repository instanceof Repository);
        this._rawData = rawData;
        this._remoteId = rawData.id;
        if (this._remoteId)
            this.ensureNamedStaticMap('remoteId')[this._remoteId] = this;
    }

    updateSingleton(rawData)
    {
        super.updateSingleton(rawData);

        console.assert(+this._rawData['time'] == +rawData['time']);
        console.assert(this._rawData['revision'] == rawData['revision']);

        if (rawData.authorName)
            this._rawData.authorName = rawData.authorName;
        if (rawData.message)
            this._rawData.message = rawData.message;
    }

    repository() { return this._repository; }
    time() { return new Date(this._rawData['time']); }
    author() { return this._rawData['authorName']; }
    revision() { return this._rawData['revision']; }
    message() { return this._rawData['message']; }
    url() { return this._repository.urlForRevision(this._rawData['revision']); }

    label()
    {
        var revision = this.revision();
        if (parseInt(revision) == revision) // e.g. r12345
            return 'r' + revision;
        else if (revision.length == 40) // e.g. git hash
            return revision.substring(0, 8);
        return revision;
    }
    title() { return this._repository.name() + ' at ' + this.label(); }

    diff(previousCommit)
    {
        if (this == previousCommit)
            previousCommit = null;

        var repository = this._repository;
        if (!previousCommit)
            return {from: null, to: this.revision(), repository: repository, label: this.label(), url: this.url()};

        var to = this.revision();
        var from = previousCommit.revision();
        var label = null;
        if (parseInt(to) == to) { // e.g. r12345.
            from = parseInt(from) + 1;
            label = `r${from}-r${this.revision()}`;
        } else if (to.length == 40) { // e.g. git hash
            label = `${from.substring(0, 8)}..${to.substring(0, 8)}`;
        } else
            label = `${from} - ${to}`;

        return {from: from, to: to, repository: repository, label: label, url: repository.urlForRevisionRange(from, to)};
    }

    static fetchBetweenRevisions(repository, from, to)
    {
        var params = [];
        if (from && to) {
            params.push(['from', from]);
            params.push(['to', to]);
        }

        var url = '../api/commits/' + repository.id() + '/?' + params.map(function (keyValue) {
            return encodeURIComponent(keyValue[0]) + '=' + encodeURIComponent(keyValue[1]);
        }).join('&');


        var cachedLogs = this._cachedCommitLogs(repository, from, to);
        if (cachedLogs)
            return new Promise(function (resolve) { resolve(cachedLogs); });

        var self = this;
        return RemoteAPI.getJSONWithStatus(url).then(function (data) {
            var commits = data['commits'].map(function (rawData) {
                rawData.repository = repository;
                return CommitLog.ensureSingleton(rawData.id, rawData);
            });
            self._cacheCommitLogs(repository, from, to, commits);
            return commits;
        });
    }

    static _cachedCommitLogs(repository, from, to)
    {
        if (!this._caches)
            return null;
        var cache = this._caches[repository.id()];
        if (!cache)
            return null;
        // FIXME: Make each commit know of its parent, then we can do a better caching. 
        return cache[from + '|' + to];
    }

    static _cacheCommitLogs(repository, from, to, logs)
    {
        if (!this._caches)
            this._caches = {};
        if (!this._caches[repository.id()])
            this._caches[repository.id()] = {};
        this._caches[repository.id()][from + '|' + to] = logs;
    }
}

if (typeof module != 'undefined')
    module.exports.CommitLog = CommitLog;
