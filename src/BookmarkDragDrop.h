#pragma once

#include <QMimeData>
#include <QStringList>
#include <QToolButton>
#include <QTreeWidget>
#include <functional>

namespace BookmarkDragDrop {
constexpr int NodeIdRole = Qt::UserRole + 1;
constexpr int FolderRole = Qt::UserRole + 2;
constexpr int SourceIndexRole = Qt::UserRole + 3;

struct Payload {
    QString generation;
    quint64 revision = 0;
    QStringList ids;
};

QMimeData* encode(const Payload& payload);
bool decode(const QMimeData* mime, Payload* payload);
using MimeFactory = std::function<QMimeData*(const QStringList&)>;
// Preview and commit use the same validation. A negative index appends to a folder.
using DropHandler = std::function<bool(const QMimeData*, const QString&, int, bool)>;
}

class BookmarkFolderTree : public QTreeWidget {
public:
    explicit BookmarkFolderTree(QWidget* parent = nullptr);
    BookmarkDragDrop::MimeFactory makeMime;
    BookmarkDragDrop::DropHandler handleDrop;

protected:
    void startDrag(Qt::DropActions actions) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QString dropFolderId_;
    QRect dropRect_;
    bool preview(const QMimeData* mime, const QPoint& position);
    void clearDrop();
};

class BookmarkPathButton : public QToolButton {
public:
    explicit BookmarkPathButton(QWidget* parent = nullptr);
    QString folderId;
    BookmarkDragDrop::DropHandler handleDrop;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
};
