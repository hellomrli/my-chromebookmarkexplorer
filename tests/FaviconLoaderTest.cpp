#include "FaviconLoader.h"

#include <QBuffer>
#include <QFile>
#include <QHostAddress>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <vector>

class IconServer : public QObject {
public:
    struct Response {
        QByteArray body;
        int status = 200;
        QByteArray location;
        bool hold = false;
    };

    QHash<QString, Response> routes;
    QHash<QString, int> requests;
    QList<QByteArray> headers;

    IconServer()
    {
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = server_.nextPendingConnection()) {
                sockets_.insert(socket);
                connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                    sockets_.remove(socket);
                    socket->deleteLater();
                });
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    const QByteArray data = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", data);
                    if (!data.contains("\r\n\r\n")) return;
                    disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
                    headers.append(data);
                    const QString path = QString::fromLatin1(data.split(' ').value(1));
                    ++requests[path];
                    const Response response = routes.value(path, Response{{}, 404, {}, false});
                    if (response.hold) return;
                    send(socket, response);
                });
            }
        });
    }

    ~IconServer() override
    {
        for (auto* socket : sockets_) {
            disconnect(socket, nullptr, this, nullptr);
            socket->abort();
        }
    }

    bool listen() { return server_.listen(QHostAddress::LocalHost); }
    QString url(const QString& path = QStringLiteral("/")) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(server_.serverPort()).arg(path);
    }

    void release(const QByteArray& image)
    {
        const auto sockets = sockets_;
        for (auto* socket : sockets) send(socket, Response{image, 200, {}, false});
    }

private:
    QTcpServer server_;
    QSet<QTcpSocket*> sockets_;

    void send(QTcpSocket* socket, const Response& response)
    {
        QByteArray header = "HTTP/1.1 " + QByteArray::number(response.status) + " Test\r\nConnection: close\r\n";
        header += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
        if (!response.location.isEmpty()) header += "Location: " + response.location + "\r\n";
        socket->write(header + "\r\n" + response.body);
        socket->disconnectFromHost();
    }
};

class FaviconLoaderTest : public QObject {
    Q_OBJECT

private:
    QByteArray imageData(const char* format = "PNG")
    {
        QImage image(32, 32, QImage::Format_ARGB32);
        image.fill(QColor(30, 140, 220));
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, format)) return {};
        return data;
    }

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void siteKeys()
    {
        QCOMPARE(FaviconLoader::siteKey(QStringLiteral("https://user:secret@Example.com:443/a?q=token#section")), QStringLiteral("https://example.com/"));
        QCOMPARE(FaviconLoader::siteKey(QStringLiteral("example.com/a")), QStringLiteral("https://example.com/"));
        QVERIFY(FaviconLoader::siteKey(QStringLiteral("http://example.com:8080")) != FaviconLoader::siteKey(QStringLiteral("http://example.com")));
        QVERIFY(FaviconLoader::siteKey(QStringLiteral("https://www.example.com")) != FaviconLoader::siteKey(QStringLiteral("https://example.com")));
        for (const auto& url : {"", "chrome://bookmarks", "file:///tmp/test", "javascript:alert(1)", "data:image/png;base64,test", "http:///"}) {
            QVERIFY(FaviconLoader::siteKey(QString::fromLatin1(url)).isEmpty());
        }
    }

    void loadsAndCaches_data()
    {
        QTest::addColumn<QByteArray>("format");
        QTest::newRow("png") << QByteArray("PNG");
        QTest::newRow("ico") << QByteArray("ICO");
    }

    void loadsAndCaches()
    {
        QFETCH(QByteArray, format);
        QTemporaryDir cache;
        QVERIFY(cache.isValid());
        IconServer server;
        QVERIFY(server.listen());
        const auto data = imageData(format.constData());
        QVERIFY(!data.isEmpty());
        server.routes.insert(QStringLiteral("/favicon.ico"), {data, 200, {}, false});
        {
            FaviconLoader loader(nullptr, cache.path());
            QSignalSpy ready(&loader, &FaviconLoader::iconReady);
            QUrl bookmark(server.url(QStringLiteral("/private?q=secret#part")));
            bookmark.setUserInfo(QStringLiteral("user:password"));
            QVERIFY(loader.iconForUrl(bookmark.toString()).isNull());
            QVERIFY(loader.iconForUrl(server.url(QStringLiteral("/another"))).isNull());
            QTRY_COMPARE(ready.count(), 1);
            QCOMPARE(ready.first().first().toString(), server.url());
            QCOMPARE(server.requests.value(QStringLiteral("/favicon.ico")), 1);
            QVERIFY(!loader.iconForUrl(server.url()).isNull());
            for (const auto& header : server.headers) {
                QVERIFY(!header.toLower().contains("authorization:"));
                QVERIFY(!header.toLower().contains("cookie:"));
                QVERIFY(!header.contains("private"));
                QVERIFY(!header.contains("secret"));
            }
        }
        FaviconLoader cached(nullptr, cache.path());
        cached.setNetworkEnabled(false);
        const QIcon icon = cached.iconForUrl(server.url());
        QVERIFY(!icon.isNull());
        QCOMPARE(icon.pixmap(32, 32).toImage().pixelColor(16, 16), QColor(30, 140, 220));
        QCOMPARE(server.requests.size(), 1);
    }

    void discoversRelativeIconsAfterRedirect()
    {
        QTemporaryDir cache;
        IconServer server;
        QVERIFY(server.listen());
        server.routes.insert(QStringLiteral("/"), {{}, 302, "/home/index.html", false});
        server.routes.insert(QStringLiteral("/home/index.html"), {
            "<BASE href='../assets/'><!-- <link rel=icon href=ignored> -->"
            "<link href='bad.png' rel='shortcut ICON'>"
            "<LINK HREF='logo.png?size=64&amp;v=1' REL=apple-touch-icon>", 200, {}, false});
        server.routes.insert(QStringLiteral("/assets/bad.png"), {"not an image", 200, {}, false});
        server.routes.insert(QStringLiteral("/assets/logo.png?size=64&v=1"), {imageData(), 200, {}, false});
        FaviconLoader loader(nullptr, cache.path());
        QSignalSpy ready(&loader, &FaviconLoader::iconReady);
        loader.iconForUrl(server.url());
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(server.requests.value(QStringLiteral("/favicon.ico")), 1);
        QCOMPARE(server.requests.value(QStringLiteral("/assets/bad.png")), 1);
        QCOMPARE(server.requests.value(QStringLiteral("/assets/logo.png?size=64&v=1")), 1);
        QCOMPARE(server.requests.size(), 5);
    }

    void rejectsOversizedAndInvalidImages()
    {
        QTemporaryDir cache;
        IconServer server;
        QVERIFY(server.listen());
        server.routes.insert(QStringLiteral("/favicon.ico"), {QByteArray(1024 * 1024 + 1, 'x'), 200, {}, false});
        server.routes.insert(QStringLiteral("/"), {"<link rel=icon href='file:///tmp/secret'><link rel=icon href=/invalid.png>", 200, {}, false});
        server.routes.insert(QStringLiteral("/invalid.png"), {"not an image", 200, {}, false});
        FaviconLoader loader(nullptr, cache.path());
        QSignalSpy ready(&loader, &FaviconLoader::iconReady);
        loader.iconForUrl(server.url());
        QTRY_COMPARE(server.requests.value(QStringLiteral("/invalid.png")), 1);
        QTest::qWait(50);
        for (int attempt = 0; attempt < 5; ++attempt) QVERIFY(loader.iconForUrl(server.url()).isNull());
        QTest::qWait(50);
        QCOMPARE(ready.count(), 0);
        QCOMPARE(server.requests.value(QStringLiteral("/favicon.ico")), 1);
        QCOMPARE(server.requests.size(), 3);
    }

    void limitsConcurrency()
    {
        QTemporaryDir cache;
        FaviconLoader loader(nullptr, cache.path());
        QSignalSpy ready(&loader, &FaviconLoader::iconReady);
        std::vector<std::unique_ptr<IconServer>> servers;
        for (int index = 0; index < 8; ++index) {
            auto server = std::make_unique<IconServer>();
            QVERIFY(server->listen());
            server->routes.insert(QStringLiteral("/favicon.ico"), {{}, 200, {}, true});
            loader.iconForUrl(server->url());
            servers.push_back(std::move(server));
        }
        QTRY_COMPARE(servers[3]->requests.size(), 1);
        QTest::qWait(50);
        for (int index = 4; index < 8; ++index) QCOMPARE(servers[index]->requests.size(), 0);
        for (int index = 0; index < 4; ++index) servers[index]->release(imageData());
        QTRY_COMPARE(ready.count(), 4);
        QTRY_COMPARE(servers[7]->requests.size(), 1);
        for (int index = 4; index < 8; ++index) servers[index]->release(imageData());
        QTRY_COMPARE(ready.count(), 8);
    }

    void timesOutAndFallsBack()
    {
        QTemporaryDir cache;
        IconServer server;
        QVERIFY(server.listen());
        server.routes.insert(QStringLiteral("/favicon.ico"), {{}, 200, {}, true});
        server.routes.insert(QStringLiteral("/"), {"<link rel=icon href=/logo.png>", 200, {}, false});
        server.routes.insert(QStringLiteral("/logo.png"), {imageData(), 200, {}, false});
        FaviconLoader loader(nullptr, cache.path());
        QSignalSpy ready(&loader, &FaviconLoader::iconReady);
        loader.iconForUrl(server.url());
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 9000);
        QVERIFY(!loader.iconForUrl(server.url()).isNull());
    }

    void disabledLoaderDoesNotRequest()
    {
        QTemporaryDir cache;
        IconServer server;
        QVERIFY(server.listen());
        FaviconLoader loader(nullptr, cache.path());
        loader.setNetworkEnabled(false);
        QVERIFY(loader.iconForUrl(server.url()).isNull());
        QTest::qWait(50);
        QVERIFY(server.requests.isEmpty());
    }
};

QTEST_MAIN(FaviconLoaderTest)
#include "FaviconLoaderTest.moc"
