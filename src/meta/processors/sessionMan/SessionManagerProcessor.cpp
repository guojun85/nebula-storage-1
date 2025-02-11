/* Copyright (c) 2020 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#include "meta/processors/sessionMan/SessionManagerProcessor.h"

namespace nebula {
namespace meta {

void CreateSessionProcessor::process(const cpp2::CreateSessionReq& req) {
    folly::SharedMutex::WriteHolder wHolder(LockUtils::sessionLock());
    const auto& user = req.get_user();
    auto ret = userExist(user);
    if (ret != nebula::cpp2::ErrorCode::SUCCEEDED) {
        LOG(ERROR) << "User does not exist, errorCode: "
                   << apache::thrift::util::enumNameSafe(ret);
        handleErrorCode(ret);
        onFinished();
        return;
    }

    cpp2::Session session;
    // The sessionId is generated by microsecond timestamp
    session.set_session_id(time::WallClock::fastNowInMicroSec());
    session.set_create_time(session.get_session_id());
    session.set_update_time(session.get_create_time());
    session.set_user_name(user);
    session.set_graph_addr(req.get_graph_addr());
    session.set_client_ip(req.get_client_ip());

    std::vector<kvstore::KV> data;
    data.emplace_back(MetaServiceUtils::sessionKey(session.get_session_id()),
                      MetaServiceUtils::sessionVal(session));
    resp_.set_session(session);
    ret = doSyncPut(std::move(data));
    if (ret != nebula::cpp2::ErrorCode::SUCCEEDED) {
        LOG(ERROR) << "Put data error on meta server, errorCode: "
                   << apache::thrift::util::enumNameSafe(ret);
    }
    handleErrorCode(ret);
    onFinished();
}


void UpdateSessionsProcessor::process(const cpp2::UpdateSessionsReq& req) {
    folly::SharedMutex::WriteHolder wHolder(LockUtils::sessionLock());
    std::vector<kvstore::KV> data;
    for (auto& session : req.get_sessions()) {
        auto sessionId = session.get_session_id();
        auto sessionKey = MetaServiceUtils::sessionKey(sessionId);
        auto ret = doGet(sessionKey);
        if (!nebula::ok(ret)) {
            auto errCode = nebula::error(ret);
            LOG(WARNING) << "Session id `" << sessionId << "' not found";
            if (errCode == nebula::cpp2::ErrorCode::E_KEY_NOT_FOUND) {
                errCode = nebula::cpp2::ErrorCode::E_SESSION_NOT_FOUND;
            }
            handleErrorCode(errCode);
            onFinished();
            return;
        }

        data.emplace_back(MetaServiceUtils::sessionKey(sessionId),
                          MetaServiceUtils::sessionVal(session));
    }
    auto ret = doSyncPut(std::move(data));
    if (ret != nebula::cpp2::ErrorCode::SUCCEEDED) {
        LOG(ERROR) << "Put data error on meta server, errorCode: "
                   << apache::thrift::util::enumNameSafe(ret);
    }

    handleErrorCode(ret);
    onFinished();
}


void ListSessionsProcessor::process(const cpp2::ListSessionsReq&) {
    folly::SharedMutex::ReadHolder rHolder(LockUtils::sessionLock());
    auto &prefix = MetaServiceUtils::sessionPrefix();
    auto ret = doPrefix(prefix);
    if (!nebula::ok(ret)) {
        handleErrorCode(nebula::error(ret));
        onFinished();
        return;
    }

    std::vector<cpp2::Session> sessions;
    auto iter = nebula::value(ret).get();
    while (iter->valid()) {
        auto session = MetaServiceUtils::parseSessionVal(iter->val());
        VLOG(3) << "List session: " << session.get_session_id();
        sessions.emplace_back(std::move(session));
        iter->next();
    }
    resp_.set_sessions(std::move(sessions));
    for (auto &session : resp_.get_sessions()) {
        LOG(INFO) << "resp list session: " << session.get_session_id();
    }
    handleErrorCode(nebula::cpp2::ErrorCode::SUCCEEDED);
    onFinished();
}


void GetSessionProcessor::process(const cpp2::GetSessionReq& req) {
    folly::SharedMutex::ReadHolder rHolder(LockUtils::sessionLock());
    auto sessionId = req.get_session_id();
    auto sessionKey = MetaServiceUtils::sessionKey(sessionId);
    auto ret = doGet(sessionKey);
    if (!nebula::ok(ret)) {
        auto errCode = nebula::error(ret);
        LOG(ERROR) << "Session id `" << sessionId << "' not found";
        if (errCode == nebula::cpp2::ErrorCode::E_KEY_NOT_FOUND) {
            errCode = nebula::cpp2::ErrorCode::E_SESSION_NOT_FOUND;
        }
        handleErrorCode(errCode);
        onFinished();
        return;
    }

    auto session = MetaServiceUtils::parseSessionVal(nebula::value(ret));
    resp_.set_session(std::move(session));
    handleErrorCode(nebula::cpp2::ErrorCode::SUCCEEDED);
    onFinished();
}

void RemoveSessionProcessor::process(const cpp2::RemoveSessionReq& req) {
    folly::SharedMutex::WriteHolder wHolder(LockUtils::sessionLock());
    auto sessionId = req.get_session_id();
    auto sessionKey = MetaServiceUtils::sessionKey(sessionId);
    auto ret = doGet(sessionKey);
    if (!nebula::ok(ret)) {
        auto errCode = nebula::error(ret);
        LOG(ERROR) << "Session id `" << sessionId << "' not found";
        if (errCode == nebula::cpp2::ErrorCode::E_KEY_NOT_FOUND) {
            errCode = nebula::cpp2::ErrorCode::E_SESSION_NOT_FOUND;
        }
        handleErrorCode(errCode);
        onFinished();
        return;
    }

    handleErrorCode(nebula::cpp2::ErrorCode::SUCCEEDED);
    doRemove(sessionKey);
}

}  // namespace meta
}  // namespace nebula
