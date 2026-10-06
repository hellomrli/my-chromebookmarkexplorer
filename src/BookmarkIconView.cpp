#include "BookmarkIconView.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QPainter>

BookmarkIconView::BookmarkIconView(QWidget* parent) : QListWidget(parent)
{
    setViewMode(QListView::IconMode);
    // Static mode disables viewport drops during layout; our drop handler owns reordering.
    setMovement(QListView::Snap);
    setResizeMode(QListView::Adjust);
    setLayoutMode(QListView::SinglePass);
    setIconSize(QSize(48, 48));
    setGridSize(QSize(128, 112));
    setSpacing(8);
    setWordWrap(true);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setAutoScroll(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setAccessibleName(QStringLiteral("书签图标视图"));
}

void BookmarkIconView::setFolderContext(const QString& folderId, bool filtered)
{
    folderId_ = folderId;
    filtered_ = filtered;
    drop_ = {};
}

QStringList BookmarkIconView::selectedNodeIds() const
{
    QStringList ids;
    for (int i = 0; i < count(); ++i) {
        if (item(i)->isSelected()) ids.append(item(i)->data(BookmarkDragDrop::NodeIdRole).toString());
    }
    return ids;
}

BookmarkIconView::DropLocation BookmarkIconView::dropLocation(const QPoint& position) const
{
    if (folderId_.isEmpty() || !viewport()->rect().contains(position)) return {};
    auto* hit = itemAt(position);
    if (hit) {
        const QRect rect = visualItemRect(hit);
        const int edge = qMax(12, rect.width() / 5);
        if (hit->data(BookmarkDragDrop::FolderRole).toBool()
            && position.x() >= rect.left() + edge && position.x() <= rect.right() - edge) {
            return {hit->data(BookmarkDragDrop::NodeIdRole).toString(), -1, rect, true};
        }
        if (filtered_) return {};
        const bool after = position.x() > rect.center().x();
        const int index = hit->data(BookmarkDragDrop::SourceIndexRole).toInt() + (after ? 1 : 0);
        const int x = after ? rect.right() + 2 : rect.left() - 2;
        return {folderId_, index, QRect(x, rect.top(), 2, rect.height()), false};
    }
    if (filtered_) return {};
    // Gaps in a wrapped row still map to that row, not to the end of the folder.
    for (int i = 0; i < count(); ++i) {
        const QRect rect = visualItemRect(item(i));
        if (position.y() <= rect.bottom() && (position.y() < rect.top() || position.x() < rect.center().x())) {
            return {folderId_, item(i)->data(BookmarkDragDrop::SourceIndexRole).toInt(),
                QRect(rect.left() - 2, rect.top(), 2, rect.height()), false};
        }
    }
    if (count() == 0) return {folderId_, -1, viewport()->rect().adjusted(6, 6, -6, -6), true};
    const QRect last = visualItemRect(item(count() - 1));
    return {folderId_, -1, QRect(last.right() + 2, last.top(), 2, last.height()), false};
}

void BookmarkIconView::setEmptyText(const QString& text)
{
    emptyText_ = text;
    viewport()->update();
}

void BookmarkIconView::startDrag(Qt::DropActions)
{
    const auto ids = selectedNodeIds();
    if (ids.isEmpty() || !makeMime) return;
    auto* mime = makeMime(ids);
    if (!mime) return;
    QDrag drag(this);
    drag.setMimeData(mime);
    if (currentItem()) drag.setPixmap(currentItem()->icon().pixmap(48, 48));
    drag.exec(Qt::MoveAction);
}

void BookmarkIconView::dragEnterEvent(QDragEnterEvent* event)
{
    BookmarkDragDrop::Payload payload;
    if ((event->possibleActions() & Qt::MoveAction) && BookmarkDragDrop::decode(event->mimeData(), &payload)) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    } else event->ignore();
}

bool BookmarkIconView::preview(const QMimeData* mime, const QPoint& position)
{
    drop_ = dropLocation(position);
    if (!drop_.valid() || !handleDrop || !handleDrop(mime, drop_.folderId, drop_.index, false)) drop_ = {};
    viewport()->update();
    return drop_.valid();
}

void BookmarkIconView::dragMoveEvent(QDragMoveEvent* event)
{
    if ((event->possibleActions() & Qt::MoveAction) && preview(event->mimeData(), event->position().toPoint())) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    } else event->ignore();
}

void BookmarkIconView::dragLeaveEvent(QDragLeaveEvent* event)
{
    drop_ = {};
    viewport()->update();
    event->accept();
}

void BookmarkIconView::dropEvent(QDropEvent* event)
{
    if ((event->possibleActions() & Qt::MoveAction) && preview(event->mimeData(), event->position().toPoint())
        && handleDrop(event->mimeData(), drop_.folderId, drop_.index, true)) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    } else event->ignore();
    drop_ = {};
    viewport()->update();
}

void BookmarkIconView::paintEvent(QPaintEvent* event)
{
    QListWidget::paintEvent(event);
    QPainter painter(viewport());
    if (count() == 0) {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(viewport()->rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, emptyText_);
    }
    if (!drop_.valid()) return;
    painter.setPen(QPen(palette().highlight().color(), 2));
    if (drop_.intoFolder) painter.drawRoundedRect(drop_.rect.adjusted(1, 1, -2, -2), 6, 6);
    else painter.fillRect(drop_.rect, palette().highlight());
}
