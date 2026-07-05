#pragma once

#include "BookmarkNode.h"

#include <QJsonDocument>
#include <QVector>

class BookmarkDocument {
public:
    bool load(const QString& filePath, QString* error = nullptr);
    bool save(const QString& filePath = {}, bool requireChromeClosed = true, QString* error = nullptr);

    QString path() const;
    bool isDirty() const;
    void setDirty(bool value);

    const std::vector<std::unique_ptr<BookmarkNode>>& roots() const;
    std::vector<BookmarkNode*> allNodes() const;
    std::vector<BookmarkNode*> folders() const;

    BookmarkNode* addFolder(BookmarkNode* parent, const QString& name);
    BookmarkNode* addBookmark(BookmarkNode* parent, const QString& name, const QString& url);
    bool add(BookmarkNode* source, BookmarkNode* parent, QString* error = nullptr);
    bool rename(BookmarkNode* node, const QString& name, QString* error = nullptr);
    bool updateUrl(BookmarkNode* node, const QString& url, QString* error = nullptr);
    bool remove(BookmarkNode* node, QString* error = nullptr);
    bool move(BookmarkNode* node, BookmarkNode* target, QString* error = nullptr);

private:
    QJsonObject topLevel_;
    QString path_;
    bool dirty_ = false;
    std::vector<std::unique_ptr<BookmarkNode>> roots_;

    void loadRoots();
    QString nextId() const;
    int maxNumericId() const;
    std::unique_ptr<BookmarkNode> takeFromParent(BookmarkNode* node);
    bool isDescendant(BookmarkNode* candidate, BookmarkNode* ancestor) const;
};
