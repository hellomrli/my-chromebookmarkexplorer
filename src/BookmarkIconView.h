#pragma once

#include "BookmarkDragDrop.h"
#include <QListWidget>

class BookmarkIconView : public QListWidget {
public:
    struct DropLocation {
        QString folderId;
        int index = -1;
        QRect rect;
        bool intoFolder = false;
        bool valid() const { return !folderId.isEmpty(); }
    };

    explicit BookmarkIconView(QWidget* parent = nullptr);
    void setFolderContext(const QString& folderId, bool filtered);
    QStringList selectedNodeIds() const;
    DropLocation dropLocation(const QPoint& position) const;
    void setEmptyText(const QString& text);
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
    QString folderId_;
    QString emptyText_;
    bool filtered_ = false;
    DropLocation drop_;
    bool preview(const QMimeData* mime, const QPoint& position);
};
