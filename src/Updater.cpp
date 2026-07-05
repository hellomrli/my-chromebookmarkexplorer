#include "Updater.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

Version Version::parse(const QString& versionString)
{
    Version v;
    QRegularExpression re(R"(v?(\d+)\.(\d+)\.(\d+))");
    auto match = re.match(versionString);
    if (match.hasMatch()) {
        v.major = match.captured(1).toInt();
        v.minor = match.captured(2).toInt();
        v.patch = match.captured(3).toInt();
    }
    return v;
}

bool Version::operator>(const Version& other) const
{
    if (major != other.major) return major > other.major;
    if (minor != other.minor) return minor > other.minor;
    return patch > other.patch;
}

QString Version::toString() const
{
    return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
}

Updater::Updater(QObject* parent)
    : QObject(parent)
    , network_(new QNetworkAccessManager(this))
{
    currentVersion_ = Version::parse(QStringLiteral("0.2.1"));
    connect(network_, &QNetworkAccessManager::finished, this, &Updater::onReplyFinished);
}

Updater::~Updater() = default;

void Updater::checkForUpdates()
{
    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/hellomrli/my-chromebookmarkexplorer/releases/latest")));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ChromeBookmarkExplorer/%1").arg(currentVersion_.toString()));
    network_->get(request);
}

void Updater::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit checkFailed(QStringLiteral("网络请求失败: %1").arg(reply->errorString()));
        return;
    }

    const QByteArray data = reply->readAll();
    parseReleaseInfo(data);
}

void Updater::parseReleaseInfo(const QByteArray& json)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError) {
        emit checkFailed(QStringLiteral("解析 JSON 失败: %1").arg(error.errorString()));
        return;
    }

    const QJsonObject obj = doc.object();
    const QString tagName = obj.value(QStringLiteral("tag_name")).toString();
    latestVersion_ = Version::parse(tagName);

    if (latestVersion_ > currentVersion_) {
        releaseNotes_ = obj.value(QStringLiteral("body")).toString();

        // 查找 Windows x64 资源
        const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
        for (const auto& assetValue : assets) {
            const QJsonObject asset = assetValue.toObject();
            const QString name = asset.value(QStringLiteral("name")).toString();
            if (name.contains(QStringLiteral("windows-x64"), Qt::CaseInsensitive) && name.endsWith(QStringLiteral(".zip"))) {
                downloadUrl_ = asset.value(QStringLiteral("browser_download_url")).toString();
                break;
            }
        }

        if (downloadUrl_.isEmpty()) {
            downloadUrl_ = obj.value(QStringLiteral("html_url")).toString();
        }

        emit updateAvailable(latestVersion_.toString(), downloadUrl_, releaseNotes_);
    } else {
        emit noUpdateAvailable();
    }
}
