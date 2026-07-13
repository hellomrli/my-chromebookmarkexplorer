#include "HealthChecker.h"

#include <QNetworkProxyFactory>
#include <QNetworkRequest>
#include <QRegularExpression>

#include <algorithm>

namespace {
constexpr int kMaximumAttempts = 2;

bool isAccessRestrictedStatus(int code)
{
    switch (code) {
    case 401: // Authentication required
    case 403: // A browser/WAF may allow the page after cookies or JavaScript checks
    case 429: // Rate limited
    case 451: // Unavailable for legal/policy reasons
        return true;
    default:
        return false;
    }
}

QUrl withScheme(QUrl url, const QString& scheme)
{
    const int oldPort = url.port(-1);
    const QString oldScheme = url.scheme().toLower();
    url.setScheme(scheme);
    if ((oldScheme == QStringLiteral("http") && oldPort == 80 && scheme == QStringLiteral("https"))
        || (oldScheme == QStringLiteral("https") && oldPort == 443 && scheme == QStringLiteral("http"))) {
        url.setPort(-1);
    }
    return url;
}
}

bool HealthResult::ok() const
{
    return state == HealthState::Healthy
        || state == HealthState::Redirected
        || state == HealthState::AccessRestricted
        || state == HealthState::Skipped;
}

bool HealthResult::reachable() const
{
    return code > 0
        || state == HealthState::Healthy
        || state == HealthState::Redirected
        || state == HealthState::AccessRestricted
        || state == HealthState::NotFound
        || state == HealthState::ClientError
        || state == HealthState::ServerError
        || state == HealthState::RedirectError;
}

bool HealthResult::definitivelyBroken() const
{
    return state == HealthState::NotFound || state == HealthState::InvalidUrl;
}

HealthChecker::HealthChecker(QObject* parent)
    : QObject(parent)
{
    // DefaultProxy follows the application proxy factory. Enable the platform
    // proxy so the checker uses the same system-level route as browser traffic;
    // setProxy() can still override it for this manager.
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    manager_.setProxy(QNetworkProxy::DefaultProxy);
    connect(&manager_, &QNetworkAccessManager::finished, this, &HealthChecker::onReplyFinished);
}

void HealthChecker::check(QVector<BookmarkNode*> nodes)
{
    if (running_) {
        return;
    }

    pending_.clear();
    states_.clear();
    activePerHost_.clear();
    total_ = 0;
    completed_ = 0;
    failed_ = 0;

    QVector<QPair<BookmarkNode*, HealthResult>> cachedResults;
    cachedResults.reserve(nodes.size());

    for (auto* node : nodes) {
        if (node == nullptr || !node->isUrl() || node->url().trimmed().isEmpty()) {
            continue;
        }

        ++total_;
        const PreparedUrl prepared = prepareUrl(node->url());
        if (cacheEnabled_ && !prepared.cacheKey.isEmpty() && cache_.contains(prepared.cacheKey)) {
            cachedResults.push_back(qMakePair(node, cache_.value(prepared.cacheKey)));
        } else {
            pending_.enqueue(node);
        }
    }

    running_ = true;
    for (const auto& cached : cachedResults) {
        complete(cached.first, prepareUrl(cached.first->url()).cacheKey, cached.second);
    }

    startMore();
    finishIfDone();
}

bool HealthChecker::isRunning() const
{
    return running_;
}

int HealthChecker::maxConcurrent() const
{
    return maxConcurrent_;
}

void HealthChecker::setMaxConcurrent(int value)
{
    if (running_) {
        return;
    }
    maxConcurrent_ = std::clamp(value, 1, 128);
}

int HealthChecker::maxConcurrentPerHost() const
{
    return maxConcurrentPerHost_;
}

void HealthChecker::setMaxConcurrentPerHost(int value)
{
    if (running_) {
        return;
    }
    maxConcurrentPerHost_ = std::clamp(value, 1, 16);
}

int HealthChecker::requestTimeoutMs() const
{
    return requestTimeoutMs_;
}

void HealthChecker::setRequestTimeoutMs(int value)
{
    if (running_) {
        return;
    }
    requestTimeoutMs_ = std::clamp(value, 100, 60000);
}

void HealthChecker::setUserAgent(const QString& userAgent)
{
    const QString trimmed = userAgent.trimmed();
    if (!trimmed.isEmpty()) {
        userAgent_ = trimmed;
    }
}

void HealthChecker::setProxy(const QString& host, int port, const QString& user, const QString& password)
{
    QNetworkProxy proxy;
    proxy.setType(QNetworkProxy::HttpProxy);
    proxy.setHostName(host);
    proxy.setPort(static_cast<quint16>(std::clamp(port, 1, 65535)));
    if (!user.isEmpty()) {
        proxy.setUser(user);
        proxy.setPassword(password);
    }
    manager_.setProxy(proxy);
}

void HealthChecker::clearProxy()
{
    manager_.setProxy(QNetworkProxy::DefaultProxy);
}

void HealthChecker::onReplyFinished(QNetworkReply* reply)
{
    auto it = states_.find(reply);
    if (it == states_.end()) {
        reply->deleteLater();
        return;
    }

    const RequestState state = it.value();
    states_.erase(it);
    releaseHost(state.hostKey);

    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (retryRequest(state, reply, code)) {
        reply->deleteLater();
        startMore();
        return;
    }
    if (code == 0 && shouldTryAlternate(reply->error()) && switchToAlternate(state)) {
        reply->deleteLater();
        startMore();
        return;
    }

    const HealthResult result = classify(reply, state);
    complete(state.node, state.cacheKey, result);
    reply->deleteLater();

    startMore();
    finishIfDone();
}

void HealthChecker::startMore()
{
    if (!running_) {
        return;
    }

    qsizetype blockedInARow = 0;
    while (!pending_.isEmpty() && states_.size() < maxConcurrent_ && blockedInARow < pending_.size()) {
        BookmarkNode* node = pending_.dequeue();
        const PreparedUrl prepared = prepareUrl(node->url());

        if (!prepared.canRequest()) {
            HealthResult result;
            result.url = node->url();
            result.finalUrl = node->url();
            result.status = prepared.immediateState == HealthState::Skipped
                ? QStringLiteral("未检测（非 HTTP）")
                : QStringLiteral("网址无效");
            result.state = prepared.immediateState;
            result.error = prepared.error;
            complete(node, prepared.cacheKey, result);
            blockedInARow = 0;
            continue;
        }

        const QString key = hostKey(prepared.primary);
        if (activePerHost_.value(key) >= maxConcurrentPerHost_) {
            pending_.enqueue(node);
            ++blockedInARow;
            continue;
        }

        blockedInARow = 0;
        RequestState state;
        state.node = node;
        state.requestUrl = prepared.primary;
        state.alternateUrl = prepared.alternate;
        state.cacheKey = prepared.cacheKey;
        state.hostKey = key;
        state.timer.start();
        startRequest(state);
    }

    finishIfDone();
}

void HealthChecker::startRequest(RequestState state)
{
    QNetworkRequest request(state.requestUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent_);
    request.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8");
    request.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8");
    request.setRawHeader("Upgrade-Insecure-Requests", "1");
    request.setRawHeader("Cache-Control", "no-cache");
    request.setRawHeader("Pragma", "no-cache");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, !state.forceHttp1);
    request.setMaximumRedirectsAllowed(10);
    request.setTransferTimeout(state.attempts == 1 ? requestTimeoutMs_ : std::min(requestTimeoutMs_ + 5000, 60000));

    QNetworkReply* reply = manager_.get(request);
    reply->setReadBufferSize(64 * 1024);

    ++activePerHost_[state.hostKey];
    states_.insert(reply, state);

    connect(reply, &QNetworkReply::redirected, this, [this, reply](const QUrl&) {
        auto it = states_.find(reply);
        if (it != states_.end()) {
            it->wasRedirected = true;
            // This is a top-level reachability probe, so follow the same
            // HTTPS-to-HTTP redirects that a browser navigation can follow.
            reply->redirectAllowed();
        }
    });

    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply] {
        auto it = states_.find(reply);
        if (it == states_.end()) {
            return;
        }

        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code <= 0) {
            return;
        }
        // A real browser uses GET. Stop after the final response headers so the
        // check remains lightweight without relying on frequently-broken HEAD.
        // Leave 3xx responses alone so QNetworkAccessManager can follow them.
        if (code >= 200 && (code < 300 || code >= 400)) {
            it->abortedAfterHeaders = true;
            reply->abort();
        }
    });
}

void HealthChecker::complete(BookmarkNode* node, const QString& cacheKey, const HealthResult& result)
{
    ++completed_;
    if (!result.ok()) {
        ++failed_;
    }

    if (cacheEnabled_ && !cacheKey.isEmpty()) {
        // Do not preserve a transient/negative answer indefinitely. Successful
        // entries may still be reused when a caller explicitly enables caching.
        if (result.ok()) {
            cache_.insert(cacheKey, result);
        } else {
            cache_.remove(cacheKey);
        }
    }

    emit resultReady(node, result);
}

void HealthChecker::finishIfDone()
{
    if (!running_ || completed_ < total_ || !pending_.isEmpty() || !states_.isEmpty()) {
        return;
    }
    running_ = false;
    emit finished(total_, failed_);
}

void HealthChecker::releaseHost(const QString& key)
{
    auto it = activePerHost_.find(key);
    if (it == activePerHost_.end()) {
        return;
    }
    if (*it <= 1) {
        activePerHost_.erase(it);
    } else {
        --(*it);
    }
}

bool HealthChecker::retryRequest(const RequestState& previous, QNetworkReply* reply, int code)
{
    if (previous.attempts >= kMaximumAttempts) {
        return false;
    }

    const auto error = reply->error();
    const bool retryableStatus = isRetryableHttpStatus(code);
    if (previous.abortedAfterHeaders && !retryableStatus) {
        return false;
    }
    const bool retryable = retryableStatus
        || (code == 0 && isRetryableNetworkError(error));
    if (!retryable) {
        return false;
    }

    RequestState next = previous;
    ++next.attempts;
    next.alternateUrl = QUrl();
    next.abortedAfterHeaders = false;
    // The retry remains on the same URL. HTTP/1.1 fallback avoids false
    // negatives caused by a broken HTTP/2 server or intermediary.
    next.forceHttp1 = true;
    next.hostKey = hostKey(next.requestUrl);
    startRequest(next);
    return true;
}

bool HealthChecker::switchToAlternate(const RequestState& previous)
{
    if (!previous.alternateUrl.isValid() || previous.alternateUrl.isEmpty()) {
        return false;
    }

    RequestState alternate = previous;
    alternate.requestUrl = previous.alternateUrl;
    alternate.alternateUrl = QUrl();
    alternate.hostKey = hostKey(alternate.requestUrl);
    alternate.attempts = 1;
    alternate.forceHttp1 = false;
    alternate.abortedAfterHeaders = false;
    alternate.wasRedirected = false;
    alternate.usedAlternateUrl = true;
    startRequest(alternate);
    return true;
}

HealthResult HealthChecker::classify(QNetworkReply* reply, const RequestState& state) const
{
    HealthResult result;
    result.url = state.node ? state.node->url() : QString();
    result.code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.elapsedMs = static_cast<int>(state.timer.elapsed());
    result.attempts = state.attempts;
    result.finalUrl = reply->url().toString();
    result.error = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();

    if (state.abortedAfterHeaders && reply->error() == QNetworkReply::OperationCanceledError) {
        result.error.clear();
    }

    if (result.code >= 200 && result.code < 300) {
        const bool redirected = state.wasRedirected || state.usedAlternateUrl;
        result.state = redirected ? HealthState::Redirected : HealthState::Healthy;
        result.status = redirected ? QStringLiteral("跳转") : QStringLiteral("正常");
        result.error.clear();
        return result;
    }

    if (result.code >= 300 && result.code < 400) {
        if (reply->error() == QNetworkReply::TooManyRedirectsError
            || reply->error() == QNetworkReply::InsecureRedirectError) {
            result.state = HealthState::RedirectError;
            result.status = QStringLiteral("重定向异常");
        } else {
            result.state = HealthState::Redirected;
            result.status = QStringLiteral("跳转");
        }
        return result;
    }

    if (result.code == 407) {
        result.state = HealthState::ConnectionError;
        result.status = QStringLiteral("代理需要认证");
        return result;
    }

    if (isAccessRestrictedStatus(result.code)) {
        result.state = HealthState::AccessRestricted;
        if (result.code == 401) {
            result.status = QStringLiteral("需要登录");
        } else if (result.code == 429) {
            result.status = QStringLiteral("访问限流");
        } else {
            result.status = QStringLiteral("访问受限");
        }
        return result;
    }

    if (result.code == 404 || result.code == 410) {
        result.state = HealthState::NotFound;
        result.status = QStringLiteral("链接不存在");
        return result;
    }

    if (result.code >= 400 && result.code < 500) {
        result.state = HealthState::ClientError;
        result.status = QStringLiteral("客户端错误");
        return result;
    }

    if (result.code >= 500) {
        result.state = HealthState::ServerError;
        result.status = QStringLiteral("服务暂时异常");
        return result;
    }

    switch (reply->error()) {
    case QNetworkReply::TimeoutError:
    case QNetworkReply::ProxyTimeoutError:
        result.state = HealthState::Timeout;
        result.status = QStringLiteral("超时");
        break;
    case QNetworkReply::HostNotFoundError:
        result.state = HealthState::DnsError;
        result.status = QStringLiteral("域名解析失败");
        break;
    case QNetworkReply::ProxyNotFoundError:
    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyConnectionClosedError:
    case QNetworkReply::ProxyAuthenticationRequiredError:
        result.state = HealthState::ConnectionError;
        result.status = QStringLiteral("代理连接失败");
        break;
    case QNetworkReply::SslHandshakeFailedError:
        result.state = HealthState::TlsError;
        result.status = QStringLiteral("TLS/证书错误");
        break;
    case QNetworkReply::TooManyRedirectsError:
    case QNetworkReply::InsecureRedirectError:
        result.state = HealthState::RedirectError;
        result.status = QStringLiteral("重定向异常");
        break;
    case QNetworkReply::ProtocolUnknownError:
    case QNetworkReply::ProtocolInvalidOperationError:
        result.state = HealthState::InvalidUrl;
        result.status = QStringLiteral("协议不支持");
        break;
    default:
        result.state = HealthState::ConnectionError;
        result.status = result.error.isEmpty() ? QStringLiteral("未知") : QStringLiteral("连接失败");
        break;
    }
    return result;
}

HealthChecker::PreparedUrl HealthChecker::prepareUrl(const QString& text)
{
    PreparedUrl prepared;
    const QString trimmed = text.trimmed();
    prepared.cacheKey = trimmed;

    QUrl parsed(trimmed, QUrl::TolerantMode);
    static const QRegularExpression explicitSchemePattern(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*://"));
    static const QRegularExpression nonHierarchicalSchemePattern(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:(?![0-9]+(?:[/?#]|$))"));
    const bool hadScheme = explicitSchemePattern.match(trimmed).hasMatch()
        || nonHierarchicalSchemePattern.match(trimmed).hasMatch();
    if (!hadScheme) {
        parsed = QUrl(
            trimmed.startsWith(QStringLiteral("//"))
                ? QStringLiteral("https:") + trimmed
                : QStringLiteral("https://") + trimmed,
            QUrl::TolerantMode);
    }

    const QString scheme = parsed.scheme().toLower();
    if (hadScheme && scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        prepared.error = QStringLiteral("仅检测 HTTP/HTTPS；该书签仍可能由浏览器或其他应用正常打开");
        prepared.immediateState = HealthState::Skipped;
        return prepared;
    }

    if (!parsed.isValid() || parsed.host().isEmpty()) {
        prepared.error = QStringLiteral("网址格式无效");
        prepared.immediateState = HealthState::InvalidUrl;
        return prepared;
    }

    parsed.setScheme(scheme);
    parsed.setFragment(QString());
    prepared.primary = parsed;
    prepared.cacheKey = parsed.toString(QUrl::FullyEncoded);

    if (!hadScheme && parsed.port(-1) == -1) {
        prepared.alternate = withScheme(parsed, QStringLiteral("http"));
    } else if (scheme == QStringLiteral("http") && parsed.port(-1) == -1) {
        // Browsers may transparently upgrade an old HTTP bookmark via HSTS or
        // HTTPS-first behavior even when port 80 no longer responds.
        prepared.alternate = withScheme(parsed, QStringLiteral("https"));
    }

    return prepared;
}

QString HealthChecker::hostKey(const QUrl& url)
{
    const int defaultPort = url.scheme() == QStringLiteral("https") ? 443 : 80;
    return QStringLiteral("%1:%2").arg(url.host().toLower()).arg(url.port(defaultPort));
}

bool HealthChecker::isRetryableNetworkError(QNetworkReply::NetworkError error)
{
    switch (error) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::SslHandshakeFailedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::UnknownNetworkError:
    case QNetworkReply::ProxyConnectionClosedError:
    case QNetworkReply::ProxyTimeoutError:
    case QNetworkReply::ProtocolFailure:
        return true;
    default:
        return false;
    }
}

bool HealthChecker::shouldTryAlternate(QNetworkReply::NetworkError error)
{
    switch (error) {
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::SslHandshakeFailedError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::UnknownNetworkError:
    case QNetworkReply::ProtocolFailure:
        return true;
    default:
        return false;
    }
}

bool HealthChecker::isRetryableHttpStatus(int code)
{
    switch (code) {
    case 408:
    case 425:
    case 500:
    case 502:
    case 503:
    case 504:
        return true;
    default:
        return false;
    }
}
