#include "ChromeProfiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include <algorithm>
QString ChromeProfile::label() const
{
    return QStringLiteral("%1 (%2)").arg(name, QFileInfo(path).fileName());
}

QString defaultChromeUserDataDir()
{
#ifdef Q_OS_WIN
    const QString localAppData = QString::fromLocal8Bit(qgetenv("LOCALAPPDATA"));
    if (localAppData.isEmpty()) {
        return {};
    }
    return QDir(localAppData).filePath(QStringLiteral("Google/Chrome/User Data"));
#else
    const QString home = QDir::homePath();
    const QString chrome = QDir(home).filePath(QStringLiteral(".config/google-chrome"));
    if (QDir(chrome).exists()) {
        return chrome;
    }
    return QDir(home).filePath(QStringLiteral(".config/chromium"));
#endif
}

static QString profileSortKey(const QString& name)
{
    if (name == QStringLiteral("Default")) {
        return QStringLiteral("0-Default");
    }
    if (name.startsWith(QStringLiteral("Profile "))) {
        bool ok = false;
        const int index = name.section(' ', -1).toInt(&ok);
        if (ok) {
            return QStringLiteral("1-%1").arg(index, 4, 10, QLatin1Char('0'));
        }
    }
    return QStringLiteral("2-") + name;
}

static QString readProfileName(const QString& profileDir)
{
    QFile file(QDir(profileDir).filePath(QStringLiteral("Preferences")));
    if (file.open(QIODevice::ReadOnly)) {
        const auto document = QJsonDocument::fromJson(file.readAll());
        const QString name = document.object().value("profile").toObject().value("name").toString();
        if (!name.trimmed().isEmpty()) {
            return name.trimmed();
        }
    }
    return QFileInfo(profileDir).fileName();
}

QVector<ChromeProfile> discoverChromeProfiles()
{
    QVector<ChromeProfile> profiles;
    const QDir base(defaultChromeUserDataDir());
    if (!base.exists()) {
        return profiles;
    }

    QFileInfoList dirs = base.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    std::sort(dirs.begin(), dirs.end(), [](const QFileInfo& left, const QFileInfo& right) {
        return profileSortKey(left.fileName()) < profileSortKey(right.fileName());
    });

    for (const auto& dir : dirs) {
        const QString bookmarks = QDir(dir.absoluteFilePath()).filePath(QStringLiteral("Bookmarks"));
        if (!QFile::exists(bookmarks)) {
            continue;
        }
        profiles.push_back({readProfileName(dir.absoluteFilePath()), dir.absoluteFilePath(), bookmarks});
    }
    return profiles;
}

bool isChromeRunning()
{
#ifdef Q_OS_WIN
    QProcess process;
    process.start(QStringLiteral("tasklist"), {QStringLiteral("/FI"), QStringLiteral("IMAGENAME eq chrome.exe"), QStringLiteral("/NH")});
    if (!process.waitForFinished(5000)) {
        return false;
    }
    return QString::fromLocal8Bit(process.readAllStandardOutput()).contains(QStringLiteral("chrome.exe"), Qt::CaseInsensitive);
#else
    const QStringList names = {QStringLiteral("chrome"), QStringLiteral("google-chrome"), QStringLiteral("chromium"), QStringLiteral("chromium-browser")};
    for (const auto& name : names) {
        QProcess process;
        process.start(QStringLiteral("pgrep"), {QStringLiteral("-x"), name});
        if (process.waitForFinished(1000) && process.exitCode() == 0) {
            return true;
        }
    }
    return false;
#endif
}
