#include "FaviconLoader.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextDocumentFragment>
#include <QTimer>

#include <memory>

namespace {
constexpr int ImageLimit = 1024 * 1024;
constexpr int PageLimit = 256 * 1024;
constexpr int MaxConcurrent = 4;

bool isWebUrl(const QUrl& url)
{
    return url.isValid() && !url.host().isEmpty()
        && (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"));
}

QImage readIcon(const QByteArray& data)
{
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > 2048 || size.height() > 2048) return {};
    const QImage image = reader.read();
    return image.isNull() ? QImage() : image.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QString attribute(const QString& tag, const QString& name)
{
    const QRegularExpression expression(QStringLiteral("\\s%1\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))").arg(name),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = expression.match(tag);
    for (int capture = 1; capture <= 3; ++capture) {
        if (!match.captured(capture).isNull()) {
            return QTextDocumentFragment::fromHtml(match.captured(capture)).toPlainText().trimmed();
        }
    }
    return {};
}

QList<QUrl> pageIcons(const QByteArray& data, const QUrl& pageUrl)
{
    QString html = QString::fromUtf8(data);
    html.remove(QRegularExpression(QStringLiteral("<!--.*?-->"), QRegularExpression::DotMatchesEverythingOption));
    QUrl baseUrl = pageUrl;
    const QRegularExpression baseTag(QStringLiteral("<base\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    const auto baseMatch = baseTag.match(html);
    if (baseMatch.hasMatch()) {
        const QUrl declaredBase = pageUrl.resolved(QUrl(attribute(baseMatch.captured(), QStringLiteral("href"))));
        if (isWebUrl(declaredBase)) baseUrl = declaredBase;
    }
    const QRegularExpression linkTag(QStringLiteral("<link\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    auto matches = linkTag.globalMatch(html);
    QList<QUrl> candidates;
    while (matches.hasNext() && candidates.size() < 4) {
        const QString tag = matches.next().captured();
        const auto relations = attribute(tag, QStringLiteral("rel")).toLower().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (!relations.contains(QStringLiteral("icon")) && !relations.contains(QStringLiteral("apple-touch-icon"))
            && !relations.contains(QStringLiteral("apple-touch-icon-precomposed"))) continue;
        const QString href = attribute(tag, QStringLiteral("href"));
        if (href.isEmpty()) continue;
        QUrl candidate = baseUrl.resolved(QUrl(href));
        candidate.setUserInfo({});
        candidate.setFragment({});
        if (isWebUrl(candidate) && !candidates.contains(candidate)
            && !(pageUrl.scheme() == QStringLiteral("https") && candidate.scheme() == QStringLiteral("http"))) {
            candidates.append(candidate);
        }
    }
    return candidates;
}
}

FaviconLoader::FaviconLoader(QObject* parent, const QString& cacheDirectory)
    : QObject(parent), cacheDirectory_(cacheDirectory.isEmpty()
          ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/favicons")
          : cacheDirectory)
{
}

QString FaviconLoader::siteKey(const QString& input)
{
    QUrl url(input.trimmed());
    if (url.scheme().isEmpty()) url = QUrl(QStringLiteral("https://") + input.trimmed());
    if (!isWebUrl(url)) return {};
    url.setUserInfo({});
    url.setPath(QStringLiteral("/"));
    url.setQuery({});
    url.setFragment({});
    if ((url.scheme() == QStringLiteral("https") && url.port() == 443)
        || (url.scheme() == QStringLiteral("http") && url.port() == 80)) url.setPort(-1);
    return url.toString(QUrl::FullyEncoded);
}

QString FaviconLoader::cachePath(const QString& site) const
{
    const auto digest = QCryptographicHash::hash(site.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(cacheDirectory_).filePath(QString::fromLatin1(digest) + QStringLiteral(".png"));
}

QIcon FaviconLoader::iconForUrl(const QString& url)
{
    const QString site = siteKey(url);
    if (site.isEmpty()) return {};
    if (!icons_.contains(site)) {
        QFile file(cachePath(site));
        QImage image;
        if (file.size() <= ImageLimit && file.open(QIODevice::ReadOnly)) image = readIcon(file.read(ImageLimit + 1));
        icons_.insert(site, image.isNull() ? QIcon() : QIcon(QPixmap::fromImage(image)));
        if (!image.isNull() && QFileInfo(file).lastModified().addDays(30) > QDateTime::currentDateTime()) requested_.insert(site);
    }
    if (networkEnabled_ && !requested_.contains(site)) {
        requested_.insert(site);
        pending_.enqueue(site);
        QTimer::singleShot(0, this, &FaviconLoader::startNext);
    }
    return icons_.value(site);
}

void FaviconLoader::startNext()
{
    while (networkEnabled_ && active_ < MaxConcurrent && !pending_.isEmpty()) {
        const QString site = pending_.dequeue();
        ++active_;
        request(QUrl(site).resolved(QUrl(QStringLiteral("/favicon.ico"))), ImageLimit,
            [this, site](const QByteArray& data, const QUrl&) {
                const QImage image = readIcon(data);
                if (!image.isNull()) finish(site, image);
                else fetchPage(site);
            });
    }
}

void FaviconLoader::fetchPage(const QString& site)
{
    request(QUrl(site), PageLimit, [this, site](const QByteArray& data, const QUrl& finalUrl) {
        fetchCandidate(site, pageIcons(data, finalUrl));
    });
}

void FaviconLoader::fetchCandidate(const QString& site, QList<QUrl> candidates)
{
    if (candidates.isEmpty()) {
        finish(site);
        return;
    }
    const QUrl candidate = candidates.takeFirst();
    request(candidate, ImageLimit, [this, site, candidates](const QByteArray& data, const QUrl&) {
        const QImage image = readIcon(data);
        if (!image.isNull()) finish(site, image);
        else fetchCandidate(site, candidates);
    });
}

void FaviconLoader::request(const QUrl& url, int limit, std::function<void(const QByteArray&, const QUrl&)> callback)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ChromeBookmarkExplorer/0.2"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(4);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    auto* reply = manager_.get(request);
    reply->setReadBufferSize(limit + 1);
    auto data = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [reply, data, limit] {
        data->append(reply->read(limit + 1 - data->size()));
        if (data->size() > limit) reply->abort();
    });
    auto* timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(6000);
    connect(reply, &QNetworkReply::finished, this, [reply, timer, data, callback] {
        timer->stop();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool success = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const QUrl finalUrl = reply->url();
        reply->deleteLater();
        callback(success ? *data : QByteArray(), finalUrl);
    });
}

void FaviconLoader::finish(const QString& site, const QImage& image)
{
    --active_;
    if (!image.isNull()) {
        const QIcon icon(QPixmap::fromImage(image));
        icons_.insert(site, icon);
        if (QDir().mkpath(cacheDirectory_)) {
            QSaveFile file(cachePath(site));
            if (file.open(QIODevice::WriteOnly) && image.save(&file, "PNG")) file.commit();
        }
        emit iconReady(site, icon);
    }
    startNext();
}
