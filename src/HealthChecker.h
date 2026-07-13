#pragma once

#include "BookmarkNode.h"

#include <QElapsedTimer>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QObject>
#include <QQueue>
#include <QUrl>

enum class HealthState {
    Unknown,
    Healthy,
    Redirected,
    AccessRestricted,
    NotFound,
    ClientError,
    ServerError,
    Timeout,
    DnsError,
    TlsError,
    ConnectionError,
    RedirectError,
    InvalidUrl,
    Skipped
};

struct HealthResult {
    QString url;
    QString status;
    int code = 0;
    int elapsedMs = 0;
    int attempts = 0;
    QString finalUrl;
    QString error;
    HealthState state = HealthState::Unknown;

    // “正常”表示该书签可访问，或服务器明确表示需要登录、限流等访问限制。
    bool ok() const;
    // 只要收到了 HTTP 响应，就说明目标服务器是可达的，即使内容本身返回错误。
    bool reachable() const;
    // 仅把 404/410 和无效网址视为可较有把握认定的失效链接。
    bool definitivelyBroken() const;
};

class HealthChecker : public QObject {
    Q_OBJECT

public:
    explicit HealthChecker(QObject* parent = nullptr);
    void check(QVector<BookmarkNode*> nodes);
    bool isRunning() const;
    int maxConcurrent() const;
    void setMaxConcurrent(int value);
    int maxConcurrentPerHost() const;
    void setMaxConcurrentPerHost(int value);
    int requestTimeoutMs() const;
    void setRequestTimeoutMs(int value);

    void setUserAgent(const QString& userAgent);
    QString userAgent() const { return userAgent_; }

    void setProxy(const QString& host, int port, const QString& user = {}, const QString& password = {});
    void clearProxy();

    // Optional per-instance cache: successful results only, for the lifetime of this object.
    void setCacheEnabled(bool enabled) { cacheEnabled_ = enabled; }
    bool cacheEnabled() const { return cacheEnabled_; }
    void clearCache() { cache_.clear(); }
    QHash<QString, HealthResult> cache() const { return cache_; }

signals:
    void resultReady(BookmarkNode* node, const HealthResult& result);
    void finished(int total, int failed);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    struct PreparedUrl {
        QUrl primary;
        QUrl alternate;
        QString cacheKey;
        QString error;
        HealthState immediateState = HealthState::Unknown;

        bool canRequest() const { return primary.isValid() && !primary.isEmpty(); }
    };

    struct RequestState {
        BookmarkNode* node = nullptr;
        QElapsedTimer timer;
        QUrl requestUrl;
        QUrl alternateUrl;
        QString cacheKey;
        QString hostKey;
        int attempts = 1;
        bool forceHttp1 = false;
        bool abortedAfterHeaders = false;
        bool wasRedirected = false;
        bool usedAlternateUrl = false;
    };

    QNetworkAccessManager manager_;
    QQueue<BookmarkNode*> pending_;
    QHash<QNetworkReply*, RequestState> states_;
    QHash<QString, int> activePerHost_;
    QHash<QString, HealthResult> cache_;
    int total_ = 0;
    int completed_ = 0;
    int failed_ = 0;
    int maxConcurrent_ = 8;
    int maxConcurrentPerHost_ = 2;
    int requestTimeoutMs_ = 15000;
    bool running_ = false;
    // 实时测活默认不使用结果缓存，避免一次临时失败在后续检查中持续误报。
    bool cacheEnabled_ = false;
    QString userAgent_ = QStringLiteral(
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/150.0.0.0 Safari/537.36");

    void startMore();
    void startRequest(RequestState state);
    void complete(BookmarkNode* node, const QString& cacheKey, const HealthResult& result);
    void finishIfDone();
    void releaseHost(const QString& hostKey);
    bool retryRequest(const RequestState& previous, QNetworkReply* reply, int code);
    bool switchToAlternate(const RequestState& previous);
    HealthResult classify(QNetworkReply* reply, const RequestState& state) const;

    static PreparedUrl prepareUrl(const QString& text);
    static QString hostKey(const QUrl& url);
    static bool isRetryableNetworkError(QNetworkReply::NetworkError error);
    static bool shouldTryAlternate(QNetworkReply::NetworkError error);
    static bool isRetryableHttpStatus(int code);
};
