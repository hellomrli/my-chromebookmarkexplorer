#include "BookmarkNode.h"
#include "HealthChecker.h"

#include <QEventLoop>
#include <QHostAddress>
#include <QJsonObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

class LocalHttpServer final : public QObject {
    Q_OBJECT

public:
    explicit LocalHttpServer(QObject* parent = nullptr)
        : QObject(parent)
    {
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = server_.nextPendingConnection()) {
                buffers_.insert(socket, {});
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readRequest(socket); });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                    if (socket->property("counted-active").toBool()) {
                        --activeRequests_;
                    }
                    buffers_.remove(socket);
                    socket->deleteLater();
                });
            }
        });
    }

    ~LocalHttpServer() override
    {
        server_.close();
        const auto sockets = buffers_.keys();
        for (auto* socket : sockets) {
            QObject::disconnect(socket, nullptr, this, nullptr);
            socket->abort();
        }
        buffers_.clear();
    }

    bool listen()
    {
        return server_.listen(QHostAddress::LocalHost, 0);
    }

    QUrl url(const QString& path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(server_.serverPort()).arg(path));
    }

    int requestCount(const QString& path) const { return requestCounts_.value(path); }
    QString lastMethod(const QString& path) const { return lastMethods_.value(path); }
    int maximumActiveRequests() const { return maximumActiveRequests_; }

private:
    QTcpServer server_;
    QHash<QTcpSocket*, QByteArray> buffers_;
    QHash<QString, int> requestCounts_;
    QHash<QString, QString> lastMethods_;
    int activeRequests_ = 0;
    int maximumActiveRequests_ = 0;

    void readRequest(QTcpSocket* socket)
    {
        auto it = buffers_.find(socket);
        if (it == buffers_.end()) {
            return;
        }
        it.value().append(socket->readAll());
        const int headerEnd = it.value().indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            return;
        }

        const QList<QByteArray> requestLine = it.value().left(it.value().indexOf("\r\n")).split(' ');
        if (requestLine.size() < 2) {
            socket->abort();
            return;
        }

        disconnect(socket, &QTcpSocket::readyRead, nullptr, nullptr);
        const QString method = QString::fromLatin1(requestLine.at(0));
        const QUrl target = QUrl::fromEncoded(requestLine.at(1));
        const QString path = target.path();
        ++requestCounts_[path];
        lastMethods_[path] = method;
        ++activeRequests_;
        maximumActiveRequests_ = std::max(maximumActiveRequests_, activeRequests_);
        socket->setProperty("counted-active", true);

        if (path == QStringLiteral("/timeout-first") && requestCounts_.value(path) == 1) {
            // Intentionally leave the first request unanswered. The checker
            // should time it out and issue one retry.
            return;
        }

        if (path == QStringLiteral("/slow")) {
            QPointer<QTcpSocket> guarded(socket);
            QTimer::singleShot(150, this, [this, guarded] {
                if (guarded) {
                    sendResponse(guarded, 200, "OK", "slow");
                }
            });
            return;
        }

        if (path == QStringLiteral("/head-sensitive")) {
            if (method == QStringLiteral("HEAD")) {
                sendResponse(socket, 403, "Forbidden", {});
            } else {
                sendResponse(socket, 200, "OK", "works with GET");
            }
            return;
        }
        if (path == QStringLiteral("/restricted")) {
            sendResponse(socket, 403, "Forbidden", "browser challenge");
            return;
        }
        if (path == QStringLiteral("/flaky-server")) {
            if (requestCounts_.value(path) == 1) {
                sendResponse(socket, 503, "Service Unavailable", "try again");
            } else {
                sendResponse(socket, 200, "OK", "recovered");
            }
            return;
        }
        if (path == QStringLiteral("/always-unavailable")) {
            sendResponse(socket, 503, "Service Unavailable", "still unavailable");
            return;
        }
        if (path == QStringLiteral("/missing")) {
            sendResponse(socket, 404, "Not Found", "missing");
            return;
        }
        if (path == QStringLiteral("/redirect")) {
            sendResponse(socket, 302, "Found", {}, {{"Location", "/ok"}});
            return;
        }

        sendResponse(socket, 200, "OK", "ok");
    }

    void sendResponse(
        QTcpSocket* socket,
        int code,
        const QByteArray& reason,
        const QByteArray& body,
        const QList<QPair<QByteArray, QByteArray>>& extraHeaders = {})
    {
        QByteArray response = "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
        response += "Connection: close\r\n";
        response += "Content-Type: text/html; charset=utf-8\r\n";
        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        for (const auto& header : extraHeaders) {
            response += header.first + ": " + header.second + "\r\n";
        }
        response += "\r\n";
        response += body;
        if (socket->property("counted-active").toBool()) {
            socket->setProperty("counted-active", false);
            --activeRequests_;
        }
        socket->write(response);
        socket->disconnectFromHost();
    }
};

class HealthCheckerTest final : public QObject {
    Q_OBJECT

private:
    struct RunResult {
        QHash<QString, HealthResult> byUrl;
        int total = -1;
        int failed = -1;
        bool timedOut = false;
    };

    static std::unique_ptr<BookmarkNode> makeNode(const QString& url)
    {
        QJsonObject object;
        object.insert(QStringLiteral("type"), QStringLiteral("url"));
        object.insert(QStringLiteral("name"), url);
        object.insert(QStringLiteral("url"), url);
        return std::make_unique<BookmarkNode>(object, QString(), nullptr);
    }

    static RunResult run(
        const QStringList& urls,
        const std::function<void(HealthChecker&)>& configure = {})
    {
        std::vector<std::unique_ptr<BookmarkNode>> owned;
        QVector<BookmarkNode*> nodes;
        owned.reserve(static_cast<size_t>(urls.size()));
        nodes.reserve(urls.size());
        for (const auto& url : urls) {
            owned.push_back(makeNode(url));
            nodes.push_back(owned.back().get());
        }

        HealthChecker checker;
        checker.setMaxConcurrent(8);
        checker.setMaxConcurrentPerHost(2);
        checker.setRequestTimeoutMs(1000);
        if (configure) {
            configure(checker);
        }

        RunResult output;
        QEventLoop loop;
        QTimer guard;
        guard.setSingleShot(true);
        guard.setInterval(10000);
        QObject::connect(&guard, &QTimer::timeout, &loop, [&] {
            output.timedOut = true;
            loop.quit();
        });
        QObject::connect(&checker, &HealthChecker::resultReady, &loop, [&](BookmarkNode*, const HealthResult& result) {
            output.byUrl.insert(result.url, result);
        });
        QObject::connect(&checker, &HealthChecker::finished, &loop, [&](int total, int failed) {
            output.total = total;
            output.failed = failed;
            loop.quit();
        });

        guard.start();
        checker.check(nodes);
        if (checker.isRunning()) {
            loop.exec();
        }
        guard.stop();
        return output;
    }

private slots:
    void cacheIsDisabledByDefault()
    {
        HealthChecker checker;
        QVERIFY(!checker.cacheEnabled());
    }

    void usesBrowserLikeGetInsteadOfHead()
    {
        LocalHttpServer server;
        QVERIFY(server.listen());
        const QString url = server.url(QStringLiteral("/head-sensitive")).toString();

        const RunResult output = run({url});

        QVERIFY(!output.timedOut);
        QCOMPARE(output.total, 1);
        QCOMPARE(output.failed, 0);
        QCOMPARE(server.requestCount(QStringLiteral("/head-sensitive")), 1);
        QCOMPARE(server.lastMethod(QStringLiteral("/head-sensitive")), QStringLiteral("GET"));
        QCOMPARE(output.byUrl.value(url).state, HealthState::Healthy);
    }

    void marksServerErrorsAsReachableButTemporary()
    {
        HealthResult result;
        result.code = 503;
        result.state = HealthState::ServerError;
        result.status = QStringLiteral("服务暂时异常");

        QVERIFY(!result.ok());
        QVERIFY(result.reachable());
        QVERIFY(!result.definitivelyBroken());
    }

    void treatsForbiddenAsReachableRestriction()
    {
        LocalHttpServer server;
        QVERIFY(server.listen());
        const QString url = server.url(QStringLiteral("/restricted")).toString();

        const RunResult output = run({url});
        const HealthResult result = output.byUrl.value(url);

        QVERIFY(!output.timedOut);
        QCOMPARE(output.failed, 0);
        QCOMPARE(result.code, 403);
        QCOMPARE(result.state, HealthState::AccessRestricted);
        QVERIFY(result.ok());
        QVERIFY(result.reachable());
        QVERIFY(!result.definitivelyBroken());
    }

    void identifiesNotFoundAsDefinitiveFailure()
    {
        LocalHttpServer server;
        QVERIFY(server.listen());
        const QString url = server.url(QStringLiteral("/missing")).toString();

        const RunResult output = run({url});
        const HealthResult result = output.byUrl.value(url);

        QVERIFY(!output.timedOut);
        QCOMPARE(output.failed, 1);
        QCOMPARE(result.code, 404);
        QCOMPARE(result.state, HealthState::NotFound);
        QVERIFY(!result.ok());
        QVERIFY(result.reachable());
        QVERIFY(result.definitivelyBroken());
    }

    void followsRedirects()
    {
        LocalHttpServer server;
        QVERIFY(server.listen());
        const QString url = server.url(QStringLiteral("/redirect")).toString();

        const RunResult output = run({url});
        const HealthResult result = output.byUrl.value(url);

        QVERIFY(!output.timedOut);
        QCOMPARE(output.failed, 0);
        QCOMPARE(result.state, HealthState::Redirected);
        QVERIFY(result.finalUrl.endsWith(QStringLiteral("/ok")));
    }

    void retriesTransientTimeout()
    {
        LocalHttpServer server;
        QVERIFY(server.listen());
        const QString url = server.url(QStringLiteral("/timeout-first")).toString();

        const RunResult output = run({url}, [](HealthChecker& checker) {
            checker.setRequestTimeoutMs(150);
        });
        const HealthResult result = output.byUrl.value(url);

        QVERIFY(!output.timedOut);
        QCOMPARE(output.failed, 0);
        QCOMPARE(server.requestCount(QStringLiteral("/timeout-first")), 2);
        QCOMPARE(result.state, HealthState::Healthy);
        QCOMPARE(result.attempts, 2);
    }

    void retriesTransientServerError()
    {
        LocalHttpServer server;
        QVERIFY(server.listen());
        const QString url = server.url(QStringLiteral("/flaky-server")).toString();

        const RunResult output = run({url});
        const HealthResult result = output.byUrl.value(url);

        QVERIFY(!output.timedOut);
        QCOMPARE(output.failed, 0);
        QCOMPARE(server.requestCount(QStringLiteral("/flaky-server")), 2);
        QCOMPARE(result.state, HealthState::Healthy);
        QCOMPARE(result.attempts, 2);
    }

    void stopsAfterOneRetryForPersistentServerError()
    {
        LocalHttpServer server;
        QVERIFY(server.listen());
        const QString url = server.url(QStringLiteral("/always-unavailable")).toString();

        const RunResult output = run({url});
        const HealthResult result = output.byUrl.value(url);

        QVERIFY(!output.timedOut);
        QCOMPARE(output.failed, 1);
        QCOMPARE(server.requestCount(QStringLiteral("/always-unavailable")), 2);
        QCOMPARE(result.state, HealthState::ServerError);
        QCOMPARE(result.attempts, 2);
    }

    void limitsConcurrentRequestsPerHost()
    {
        LocalHttpServer server;
        QVERIFY(server.listen());
        QStringList urls;
        for (int i = 0; i < 8; ++i) {
            QUrl url = server.url(QStringLiteral("/slow"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("id"), QString::number(i));
            url.setQuery(query);
            urls.push_back(url.toString());
        }

        const RunResult output = run(urls);

        QVERIFY(!output.timedOut);
        QCOMPARE(output.total, urls.size());
        QCOMPARE(output.failed, 0);
        QVERIFY(server.maximumActiveRequests() <= 2);
    }

    void reportsInvalidHttpUrlsAsBroken()
    {
        const QString url = QStringLiteral("https:///");
        const RunResult output = run({url});
        const HealthResult result = output.byUrl.value(url);

        QVERIFY(!output.timedOut);
        QCOMPARE(output.total, 1);
        QCOMPARE(output.failed, 1);
        QCOMPARE(result.state, HealthState::InvalidUrl);
        QVERIFY(result.definitivelyBroken());
    }

    void skipsNonHttpBookmarksWithoutCallingThemDead()
    {
        const QStringList urls = {
            QStringLiteral("chrome://settings/"),
            QStringLiteral("file:///C:/Users/example/document.html"),
            QStringLiteral("mailto:user@example.com")
        };
        const RunResult output = run(urls);

        QVERIFY(!output.timedOut);
        QCOMPARE(output.total, urls.size());
        QCOMPARE(output.failed, 0);
        for (const auto& url : urls) {
            const HealthResult result = output.byUrl.value(url);
            QCOMPARE(result.state, HealthState::Skipped);
            QVERIFY(result.ok());
            QVERIFY(!result.reachable());
            QVERIFY(!result.definitivelyBroken());
        }
    }
};

QTEST_GUILESS_MAIN(HealthCheckerTest)
#include "HealthCheckerTest.moc"
