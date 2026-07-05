#include "BookmarkNode.h"

#include <QJsonArray>
#include <QStringList>
#include <QTimeZone>

namespace {
QDateTime chromeEpoch()
{
    return QDateTime(QDate(1601, 1, 1), QTime(0, 0), QTimeZone::UTC);
}
}

BookmarkNode::BookmarkNode(QJsonObject object, QString root, BookmarkNode* parentNode)
    : raw(std::move(object)), rootKey(std::move(root)), parent(parentNode)
{
    if (isFolder()) {
        const auto array = raw.value("children").toArray();
        children.reserve(static_cast<size_t>(array.size()));
        for (const auto& value : array) {
            if (value.isObject()) {
                children.push_back(std::make_unique<BookmarkNode>(value.toObject(), rootKey, this));
            }
        }
    }
}

QString BookmarkNode::id() const { return raw.value("id").toString(); }
QString BookmarkNode::guid() const { return raw.value("guid").toString(); }
QString BookmarkNode::type() const { return raw.value("type").toString(); }
QString BookmarkNode::name() const { return raw.value("name").toString(); }
QString BookmarkNode::url() const { return raw.value("url").toString(); }

QString BookmarkNode::displayType() const
{
    if (isFolder()) {
        return QStringLiteral("文件夹");
    }
    if (isUrl()) {
        return QStringLiteral("书签");
    }
    return type();
}

QString BookmarkNode::path() const
{
    QStringList parts;
    const BookmarkNode* current = this;
    while (current != nullptr) {
        parts.prepend(current->name());
        current = current->parent;
    }
    return parts.join(QStringLiteral("/"));
}

QString BookmarkNode::formattedDateAdded() const
{
    const auto value = chromeTimeToDateTime(raw.value("date_added").toString());
    if (!value.isValid()) {
        return {};
    }
    return value.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

bool BookmarkNode::isFolder() const { return type() == QStringLiteral("folder"); }
bool BookmarkNode::isUrl() const { return type() == QStringLiteral("url"); }

bool BookmarkNode::isRoot() const
{
    return parent == nullptr
        && (rootKey == QStringLiteral("bookmark_bar")
            || rootKey == QStringLiteral("other")
            || rootKey == QStringLiteral("synced"));
}

void BookmarkNode::setName(const QString& value)
{
    raw.insert("name", value);
    touch();
}

void BookmarkNode::setUrl(const QString& value)
{
    raw.insert("url", value);
    touch();
}

void BookmarkNode::touch()
{
    if (isFolder()) {
        raw.insert("date_modified", currentChromeTime());
    }
    if (parent != nullptr) {
        parent->touch();
    }
}

void BookmarkNode::addTag(const QString& tag)
{
    if (!tag.isEmpty() && !tags.contains(tag)) {
        tags.append(tag);
    }
}

void BookmarkNode::removeTag(const QString& tag)
{
    tags.removeAll(tag);
}

bool BookmarkNode::hasTag(const QString& tag) const
{
    return tags.contains(tag);
}

QString BookmarkNode::tagsString() const
{
    return tags.join(QStringLiteral(", "));
}

QJsonObject BookmarkNode::toJson() const
{
    QJsonObject object = raw;
    if (isFolder()) {
        QJsonArray array;
        for (const auto& child : children) {
            array.append(child->toJson());
        }
        object.insert("children", array);
    }
    return object;
}

QDateTime chromeTimeToDateTime(const QString& value)
{
    bool ok = false;
    const qint64 micros = value.toLongLong(&ok);
    if (!ok || micros <= 0) {
        return {};
    }
    return chromeEpoch().addMSecs(micros / 1000);
}

QString currentChromeTime()
{
    const qint64 micros = chromeEpoch().msecsTo(QDateTime::currentDateTimeUtc()) * 1000;
    return QString::number(micros);
}

QString rootLabel(const QString& key)
{
    if (key == QStringLiteral("bookmark_bar")) {
        return QStringLiteral("收藏夹栏");
    }
    if (key == QStringLiteral("other")) {
        return QStringLiteral("其他收藏夹");
    }
    if (key == QStringLiteral("synced")) {
        return QStringLiteral("移动设备收藏夹");
    }
    return key;
}
