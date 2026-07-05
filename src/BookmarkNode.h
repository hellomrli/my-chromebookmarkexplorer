#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

class BookmarkNode {
public:
    QJsonObject raw;
    QString rootKey;
    BookmarkNode* parent = nullptr;
    std::vector<std::unique_ptr<BookmarkNode>> children;
    QStringList tags; // 新增：标签列表

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
    QString dateAdded() const;

    bool isFolder() const;
    bool isUrl() const;
    bool isRoot() const;

    void setName(const QString& value);
    void setUrl(const QString& value);
    void touch();

    // 标签相关
    void addTag(const QString& tag);
    void removeTag(const QString& tag);
    bool hasTag(const QString& tag) const;
    QString tagsString() const;

    QJsonObject toJson() const;
    static BookmarkNode* fromJson(const QJsonObject& object, const QString& root = {}, BookmarkNode* parentNode = nullptr);
};

QDateTime chromeTimeToDateTime(const QString& value);
QString currentChromeTime();
QString rootLabel(const QString& key);
