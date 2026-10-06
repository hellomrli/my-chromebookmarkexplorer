#pragma once

#include "BookmarkNode.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QVector>

class BookmarkDocument {
public:
    bool load(const QString& filePath, QString* error = nullptr);
    bool save(const QString& filePath = {}, bool requireChromeClosed = true, QString* error = nullptr);

    QString path() const;
    bool isDirty() const;
    // Direct edits to public BookmarkNode fields must be followed by setDirty(true).
    void setDirty(bool value);
    quint64 revision() const;
    QString generation() const;

    const std::vector<std::unique_ptr<BookmarkNode>>& roots() const;
    std::vector<BookmarkNode*> allNodes() const;
    std::vector<BookmarkNode*> folders() const;
    // Missing, empty or ambiguous IDs do not resolve to a node.
    BookmarkNode* nodeById(const QString& id) const;

    BookmarkNode* addFolder(BookmarkNode* parent, const QString& name);
    BookmarkNode* addBookmark(BookmarkNode* parent, const QString& name, const QString& url);
    bool add(BookmarkNode* source, BookmarkNode* parent, QString* error = nullptr);
    bool rename(BookmarkNode* node, const QString& name, QString* error = nullptr);
    bool updateUrl(BookmarkNode* node, const QString& url, QString* error = nullptr);
    bool remove(BookmarkNode* node, QString* error = nullptr);
    bool move(BookmarkNode* node, BookmarkNode* target, QString* error = nullptr);
    // insertionIndex is a slot in target's children BEFORE removal; -1 appends.
    // Selection is deduplicated, ancestor-filtered and ordered by document preorder.
    // Validation failure is atomic; successful no-ops preserve dirty/revision.
    bool moveNodes(const QVector<BookmarkNode*>& nodes, BookmarkNode* target,
                   int insertionIndex = -1, QString* error = nullptr);

private:
    QJsonObject topLevel_;
    QString path_;
    bool dirty_ = false;
    quint64 revision_ = 0;
    QString generation_;
    std::vector<std::unique_ptr<BookmarkNode>> roots_;

    QString sourceAbsolutePath_;
    QString sourceCanonicalPath_;
    QByteArray sourceFingerprint_;

    void loadRoots();
    bool containsNode(const BookmarkNode* node) const;
    QString nextId() const;
    quint64 maxNumericId() const;
    std::unique_ptr<BookmarkNode> takeFromParent(BookmarkNode* node);
    bool isDescendant(BookmarkNode* candidate, BookmarkNode* ancestor) const;
};
