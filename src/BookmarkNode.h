#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <memory>
#include <vector>

class BookmarkNode {
public:
    QJsonObject raw;
    QString rootKey;
    BookmarkNode* parent = nullptr;
    std::vector<std::unique_ptr<BookmarkNode>> children;

    BookmarkNode() = default;
    BookmarkNode(QJsonObject object, QString root, BookmarkNode* parentNode);

    QString id() const;
    QString guid() const;
    QString type() const;
    QString name() const;
    QString url() const;
    QString displayType() const;
    QString path() const;
    QString formattedDateAdded() const;

    bool isFolder() const;
    bool isUrl() const;
    bool isRoot() const;

    void setName(const QString& value);
    void setUrl(const QString& value);
    void touch();

    QJsonObject toJson() const;
};

QDateTime chromeTimeToDateTime(const QString& value);
QString currentChromeTime();
QString rootLabel(const QString& key);
