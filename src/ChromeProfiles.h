#pragma once

#include <QString>
#include <QVector>

struct ChromeProfile {
    QString name;
    QString path;
    QString bookmarksPath;

    QString label() const;
};

QString defaultChromeUserDataDir();
QVector<ChromeProfile> discoverChromeProfiles();
bool isChromeRunning();
