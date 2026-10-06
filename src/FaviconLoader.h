#pragma once

#include <QHash>
#include <QIcon>
#include <QImage>
#include <QNetworkAccessManager>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QUrl>

#include <functional>

class FaviconLoader : public QObject {
    Q_OBJECT

public:
    explicit FaviconLoader(QObject* parent = nullptr, const QString& cacheDirectory = {});
    QIcon iconForUrl(const QString& url);
    void setNetworkEnabled(bool enabled) { networkEnabled_ = enabled; }
    static QString siteKey(const QString& url);

signals:
    void iconReady(const QString& site, const QIcon& icon);

private:
    QNetworkAccessManager manager_;
    QHash<QString, QIcon> icons_;
    QSet<QString> requested_;
    QQueue<QString> pending_;
    QString cacheDirectory_;
    int active_ = 0;
    bool networkEnabled_ = true;

    QString cachePath(const QString& site) const;
    void startNext();
    void fetchPage(const QString& site);
    void fetchCandidate(const QString& site, QList<QUrl> candidates);
    void request(const QUrl& url, int limit, std::function<void(const QByteArray&, const QUrl&)> callback);
    void finish(const QString& site, const QImage& image = {});
};
