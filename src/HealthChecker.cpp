#include "HealthChecker.h"

#include <QNetworkRequest>

bool HealthResult::ok() const
{
    return status == QStringLiteral("正常") || status == QStringLiteral("跳转");
}

HealthChecker::HealthChecker(QObject* parent)
    : QObject(parent)
{
    connect(&manager_, &QNetworkAccessManager::finished, this, &HealthChecker::onReplyFinished);
}

void HealthChecker::check(QVector<BookmarkNode*> nodes)
{
    if (running_) {
        return;
    }
    pending_.clear();
    states_.clear();
    for (auto* node : nodes) {
        if (node != nullptr && node->isUrl() && !node->url().trimmed().isEmpty()) {
            pending_.enqueue(node);
        }
    }
    total_ = pending_.size();
    failed_ = 0;
    running_ = true;
    startMore();
    if (total_ == 0) {
        running_ = false;
        emit finished(0, 0);
    }
}

bool HealthChecker::isRunning() const
{
    return running_;
}

void HealthChecker::onReplyFinished(QNetworkReply* reply)
{
    const auto state = states_.take(reply);
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (!state.fallbackGet && (code == 403 || code == 405 || code == 501)) {
        BookmarkNode* node = state.node;
        reply->deleteLater();
        startRequest(node, true);
        return;
    }

    const HealthResult result = classify(reply, state);
    if (!result.ok()) {
        ++failed_;
    }
    emit resultReady(state.node, result);
    reply->deleteLater();

    startMore();
    if (pending_.isEmpty() && states_.isEmpty()) {
        running_ = false;
        emit finished(total_, failed_);
    }
}

void HealthChecker::startMore()
{
    while (running_ && !pending_.isEmpty() && states_.size() < maxConcurrent_) {
        startRequest(pending_.dequeue(), false);
    }
}

void HealthChecker::startRequest(BookmarkNode* node, bool fallbackGet)
{
    QUrl url(node->url().trimmed());
    if (url.scheme().isEmpty()) {
        url = QUrl(QStringLiteral("http://") + node->url().trimmed());
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ChromeBookmarkExplorer/0.1"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(8000);
    if (fallbackGet) {
        request.setRawHeader("Range", "bytes=0-0");
    }

    QNetworkReply* reply = fallbackGet ? manager_.get(request) : manager_.head(request);
    RequestState state;
    state.node = node;
    state.fallbackGet = fallbackGet;
    state.timer.start();
    states_.insert(reply, state);
}

HealthResult HealthChecker::classify(QNetworkReply* reply, const RequestState& state) const
{
    HealthResult result;
    result.url = state.node ? state.node->url() : QString();
    result.code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.elapsedMs = static_cast<int>(state.timer.elapsed());
    result.finalUrl = reply->url().toString();
    result.error = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();

    if (reply->error() == QNetworkReply::TimeoutError) {
        result.status = QStringLiteral("超时");
        return result;
    }
    if (result.code >= 200 && result.code < 300) {
        const QString original = result.url.trimmed().endsWith('/') ? result.url.trimmed().chopped(1) : result.url.trimmed();
        const QString final = result.finalUrl.endsWith('/') ? result.finalUrl.chopped(1) : result.finalUrl;
        result.status = (final != original && !original.isEmpty()) ? QStringLiteral("跳转") : QStringLiteral("正常");
        return result;
    }
    if (result.code >= 300 && result.code < 400) {
        result.status = QStringLiteral("跳转");
        return result;
    }
    if (result.code >= 400 && result.code < 500) {
        result.status = QStringLiteral("客户端错误");
        return result;
    }
    if (result.code >= 500) {
        result.status = QStringLiteral("服务端错误");
        return result;
    }
    result.status = result.error.isEmpty() ? QStringLiteral("未知") : QStringLiteral("连接失败");
    return result;
}
