/*
 * Copyright (C) 2015 Apple Inc. All rights reserved.
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

#include "config.h"
#include "InProcessIDBServer.h"

#if ENABLE(INDEXED_DATABASE)

#include "FileSystem.h"
#include "IDBConnectionToClient.h"
#include "IDBConnectionToServer.h"
#include "IDBCursorInfo.h"
#include "IDBKeyRangeData.h"
#include "IDBOpenDBRequest.h"
#include "IDBRequestData.h"
#include "IDBResultData.h"
#include "IDBValue.h"
#include "Logging.h"
#include <wtf/RunLoop.h>

namespace WebCore {

Ref<InProcessIDBServer> InProcessIDBServer::create()
{
    Ref<InProcessIDBServer> server = adoptRef(*new InProcessIDBServer);
    server->m_server->registerConnection(server->connectionToClient());
    return server;
}

Ref<InProcessIDBServer> InProcessIDBServer::create(const String& databaseDirectoryPath)
{
    Ref<InProcessIDBServer> server = adoptRef(*new InProcessIDBServer(databaseDirectoryPath));
    server->m_server->registerConnection(server->connectionToClient());
    return server;
}

InProcessIDBServer::InProcessIDBServer()
    : m_server(IDBServer::IDBServer::create(*this))
{
    relaxAdoptionRequirement();
    m_connectionToServer = IDBClient::IDBConnectionToServer::create(*this);
    m_connectionToClient = IDBServer::IDBConnectionToClient::create(*this);
}

InProcessIDBServer::InProcessIDBServer(const String& databaseDirectoryPath)
    : m_server(IDBServer::IDBServer::create(databaseDirectoryPath, *this))
{
    relaxAdoptionRequirement();
    m_connectionToServer = IDBClient::IDBConnectionToServer::create(*this);
    m_connectionToClient = IDBServer::IDBConnectionToClient::create(*this);
}

uint64_t InProcessIDBServer::identifier() const
{
    // An instance of InProcessIDBServer always has a 1:1 relationship with its instance of IDBServer.
    // Therefore the connection identifier between the two can always be "1".
    return 1;
}

IDBClient::IDBConnectionToServer& InProcessIDBServer::connectionToServer() const
{
    return *m_connectionToServer;
}

IDBServer::IDBConnectionToClient& InProcessIDBServer::connectionToClient() const
{
    return *m_connectionToClient;
}

void InProcessIDBServer::deleteDatabase(const IDBRequestData& requestData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), requestData] {
        m_server->deleteDatabase(requestData);
    });
}

void InProcessIDBServer::didDeleteDatabase(const IDBResultData& resultData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), resultData] {
        m_connectionToServer->didDeleteDatabase(resultData);
    });
}

void InProcessIDBServer::openDatabase(const IDBRequestData& requestData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), requestData] {
        m_server->openDatabase(requestData);
    });
}

void InProcessIDBServer::didOpenDatabase(const IDBResultData& resultData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), resultData] {
        m_connectionToServer->didOpenDatabase(resultData);
    });
}

void InProcessIDBServer::didAbortTransaction(const IDBResourceIdentifier& transactionIdentifier, const IDBError& error)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), transactionIdentifier, error] {
        m_connectionToServer->didAbortTransaction(transactionIdentifier, error);
    });
}

void InProcessIDBServer::didCommitTransaction(const IDBResourceIdentifier& transactionIdentifier, const IDBError& error)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), transactionIdentifier, error] {
        m_connectionToServer->didCommitTransaction(transactionIdentifier, error);
    });
}

void InProcessIDBServer::didCreateObjectStore(const IDBResultData& resultData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), resultData] {
        m_connectionToServer->didCreateObjectStore(resultData);
    });
}

void InProcessIDBServer::didDeleteObjectStore(const IDBResultData& resultData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), resultData] {
        m_connectionToServer->didDeleteObjectStore(resultData);
    });
}

void InProcessIDBServer::didClearObjectStore(const IDBResultData& resultData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), resultData] {
        m_connectionToServer->didClearObjectStore(resultData);
    });
}

void InProcessIDBServer::didCreateIndex(const IDBResultData& resultData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), resultData] {
        m_connectionToServer->didCreateIndex(resultData);
    });
}

void InProcessIDBServer::didDeleteIndex(const IDBResultData& resultData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), resultData] {
        m_connectionToServer->didDeleteIndex(resultData);
    });
}

void InProcessIDBServer::didPutOrAdd(const IDBResultData& resultData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), resultData] {
        m_connectionToServer->didPutOrAdd(resultData);
    });
}

void InProcessIDBServer::didGetRecord(const IDBResultData& resultData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), resultData] {
        m_connectionToServer->didGetRecord(resultData);
    });
}

void InProcessIDBServer::didGetCount(const IDBResultData& resultData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), resultData] {
        m_connectionToServer->didGetCount(resultData);
    });
}

void InProcessIDBServer::didDeleteRecord(const IDBResultData& resultData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), resultData] {
        m_connectionToServer->didDeleteRecord(resultData);
    });
}

void InProcessIDBServer::didOpenCursor(const IDBResultData& resultData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), resultData] {
        m_connectionToServer->didOpenCursor(resultData);
    });
}

void InProcessIDBServer::didIterateCursor(const IDBResultData& resultData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), resultData] {
        m_connectionToServer->didIterateCursor(resultData);
    });
}

void InProcessIDBServer::abortTransaction(const IDBResourceIdentifier& resourceIdentifier)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), resourceIdentifier] {
        m_server->abortTransaction(resourceIdentifier);
    });
}

void InProcessIDBServer::commitTransaction(const IDBResourceIdentifier& resourceIdentifier)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), resourceIdentifier] {
        m_server->commitTransaction(resourceIdentifier);
    });
}

void InProcessIDBServer::didFinishHandlingVersionChangeTransaction(uint64_t databaseConnectionIdentifier, const IDBResourceIdentifier& transactionIdentifier)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), databaseConnectionIdentifier, transactionIdentifier] {
        m_server->didFinishHandlingVersionChangeTransaction(databaseConnectionIdentifier, transactionIdentifier);
    });
}

void InProcessIDBServer::createObjectStore(const IDBRequestData& resultData, const IDBObjectStoreInfo& info)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), resultData, info] {
        m_server->createObjectStore(resultData, info);
    });
}

void InProcessIDBServer::deleteObjectStore(const IDBRequestData& requestData, const String& objectStoreName)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), requestData, objectStoreName] {
        m_server->deleteObjectStore(requestData, objectStoreName);
    });
}

void InProcessIDBServer::clearObjectStore(const IDBRequestData& requestData, uint64_t objectStoreIdentifier)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), requestData, objectStoreIdentifier] {
        m_server->clearObjectStore(requestData, objectStoreIdentifier);
    });
}

void InProcessIDBServer::createIndex(const IDBRequestData& requestData, const IDBIndexInfo& info)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), requestData, info] {
        m_server->createIndex(requestData, info);
    });
}

void InProcessIDBServer::deleteIndex(const IDBRequestData& requestData, uint64_t objectStoreIdentifier, const String& indexName)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), requestData, objectStoreIdentifier, indexName] {
        m_server->deleteIndex(requestData, objectStoreIdentifier, indexName);
    });
}

void InProcessIDBServer::putOrAdd(const IDBRequestData& requestData, const IDBKeyData& keyData, const IDBValue& value, const IndexedDB::ObjectStoreOverwriteMode overwriteMode)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), requestData, keyData, value, overwriteMode] {
        m_server->putOrAdd(requestData, keyData, value, overwriteMode);
    });
}

void InProcessIDBServer::getRecord(const IDBRequestData& requestData, const IDBKeyRangeData& keyRangeData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), requestData, keyRangeData] {
        m_server->getRecord(requestData, keyRangeData);
    });
}

void InProcessIDBServer::getCount(const IDBRequestData& requestData, const IDBKeyRangeData& keyRangeData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), requestData, keyRangeData] {
        m_server->getCount(requestData, keyRangeData);
    });
}

void InProcessIDBServer::deleteRecord(const IDBRequestData& requestData, const IDBKeyRangeData& keyRangeData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), requestData, keyRangeData] {
        m_server->deleteRecord(requestData, keyRangeData);
    });
}

void InProcessIDBServer::openCursor(const IDBRequestData& requestData, const IDBCursorInfo& info)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), requestData, info] {
        m_server->openCursor(requestData, info);
    });
}

void InProcessIDBServer::iterateCursor(const IDBRequestData& requestData, const IDBKeyData& key, unsigned long count)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), requestData, key, count] {
        m_server->iterateCursor(requestData, key, count);
    });
}

void InProcessIDBServer::establishTransaction(uint64_t databaseConnectionIdentifier, const IDBTransactionInfo& info)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), databaseConnectionIdentifier, info] {
        m_server->establishTransaction(databaseConnectionIdentifier, info);
    });
}

void InProcessIDBServer::fireVersionChangeEvent(IDBServer::UniqueIDBDatabaseConnection& connection, const IDBResourceIdentifier& requestIdentifier, uint64_t requestedVersion)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), databaseConnectionIdentifier = connection.identifier(), requestIdentifier, requestedVersion] {
        m_connectionToServer->fireVersionChangeEvent(databaseConnectionIdentifier, requestIdentifier, requestedVersion);
    });
}

void InProcessIDBServer::didStartTransaction(const IDBResourceIdentifier& transactionIdentifier, const IDBError& error)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), transactionIdentifier, error] {
        m_connectionToServer->didStartTransaction(transactionIdentifier, error);
    });
}

void InProcessIDBServer::didCloseFromServer(IDBServer::UniqueIDBDatabaseConnection& connection, const IDBError& error)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), databaseConnectionIdentifier = connection.identifier(), error] {
        m_connectionToServer->didCloseFromServer(databaseConnectionIdentifier, error);
    });
}

void InProcessIDBServer::notifyOpenDBRequestBlocked(const IDBResourceIdentifier& requestIdentifier, uint64_t oldVersion, uint64_t newVersion)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), requestIdentifier, oldVersion, newVersion] {
        m_connectionToServer->notifyOpenDBRequestBlocked(requestIdentifier, oldVersion, newVersion);
    });
}

void InProcessIDBServer::databaseConnectionClosed(uint64_t databaseConnectionIdentifier)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), databaseConnectionIdentifier] {
        m_server->databaseConnectionClosed(databaseConnectionIdentifier);
    });
}

void InProcessIDBServer::abortOpenAndUpgradeNeeded(uint64_t databaseConnectionIdentifier, const IDBResourceIdentifier& transactionIdentifier)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), databaseConnectionIdentifier, transactionIdentifier] {
        m_server->abortOpenAndUpgradeNeeded(databaseConnectionIdentifier, transactionIdentifier);
    });
}

void InProcessIDBServer::didFireVersionChangeEvent(uint64_t databaseConnectionIdentifier, const IDBResourceIdentifier& requestIdentifier)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), databaseConnectionIdentifier, requestIdentifier] {
        m_server->didFireVersionChangeEvent(databaseConnectionIdentifier, requestIdentifier);
    });
}

void InProcessIDBServer::openDBRequestCancelled(const IDBRequestData& requestData)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), requestData] {
        m_server->openDBRequestCancelled(requestData);
    });
}

void InProcessIDBServer::confirmDidCloseFromServer(uint64_t databaseConnectionIdentifier)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), databaseConnectionIdentifier] {
        m_server->confirmDidCloseFromServer(databaseConnectionIdentifier);
    });
}

void InProcessIDBServer::getAllDatabaseNames(const SecurityOriginData& mainFrameOrigin, const SecurityOriginData& openingOrigin, uint64_t callbackID)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), mainFrameOrigin, openingOrigin, callbackID] {
        m_server->getAllDatabaseNames(m_connectionToServer->identifier(), mainFrameOrigin, openingOrigin, callbackID);
    });
}

void InProcessIDBServer::didGetAllDatabaseNames(uint64_t callbackID, const Vector<String>& databaseNames)
{
    RunLoop::current().dispatch([this, protectedThis = Ref<InProcessIDBServer>(*this), callbackID, databaseNames] {
        m_connectionToServer->didGetAllDatabaseNames(callbackID, databaseNames);
    });
}

void InProcessIDBServer::accessToTemporaryFileComplete(const String& path)
{
    deleteFile(path);
}

} // namespace WebCore

#endif // ENABLE(INDEXED_DATABASE)
