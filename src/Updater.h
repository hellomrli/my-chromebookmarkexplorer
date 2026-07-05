#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    static Version parse(const QString& versionString);
    bool operator>(const Version& other) const;
    QString toString() const;
};

class Updater : public QObject {
    Q_OBJECT

public:
    explicit Updater(QObject* parent = nullptr);
    ~Updater();

    void checkForUpdates();
    Version currentVersion() const { return currentVersion_; }
    Version latestVersion() const { return latestVersion_; }
    QString downloadUrl() const { return downloadUrl_; }
    QString releaseNotes() const { return releaseNotes_; }

signals:
    void updateAvailable(const QString& version, const QString& url, const QString& notes);
    void noUpdateAvailable();
    void checkFailed(const QString& error);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* network_ = nullptr;
    Version currentVersion_;
    Version latestVersion_;
    QString downloadUrl_;
    QString releaseNotes_;

    void parseReleaseInfo(const QByteArray& json);
};
