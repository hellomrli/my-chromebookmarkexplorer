#include "BookmarkDragDrop.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

namespace {
constexpr auto MimeType = "application/x-chrome-bookmark-explorer-nodes";
}

QMimeData* BookmarkDragDrop::encode(const Payload& payload)
{
    auto* mime = new QMimeData;
    QJsonObject object;
    object.insert(QStringLiteral("generation"), payload.generation);
    object.insert(QStringLiteral("revision"), QString::number(payload.revision));
    object.insert(QStringLiteral("ids"), QJsonArray::fromStringList(payload.ids));
    mime->setData(MimeType, QJsonDocument(object).toJson(QJsonDocument::Compact));
    return mime;
}

bool BookmarkDragDrop::decode(const QMimeData* mime, Payload* payload)
{
    if (!mime || !payload || !mime->hasFormat(MimeType)) return false;
    const QByteArray bytes = mime->data(MimeType);
    if (bytes.isEmpty() || bytes.size() > 1024 * 1024) return false;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    const auto object = document.object();
    Payload decoded;
    decoded.generation = object.value(QStringLiteral("generation")).toString();
    bool validRevision = false;
    decoded.revision = object.value(QStringLiteral("revision")).toString().toULongLong(&validRevision);
    if (decoded.generation.isEmpty() || !validRevision || !object.value(QStringLiteral("ids")).isArray()) return false;
    const auto ids = object.value(QStringLiteral("ids")).toArray();
    for (const auto& value : ids) {
        if (!value.isString() || value.toString().isEmpty()) return false;
        decoded.ids.append(value.toString());
    }
    if (decoded.ids.isEmpty()) return false;
    *payload = decoded;
    return true;
}

BookmarkFolderTree::BookmarkFolderTree(QWidget* parent) : QTreeWidget(parent)
{
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setAutoScroll(true);
}

void BookmarkFolderTree::startDrag(Qt::DropActions)
{
    const auto* item = currentItem();
    if (!item || !item->parent() || !makeMime) return;
    auto* mime = makeMime({item->data(0, BookmarkDragDrop::NodeIdRole).toString()});
    if (!mime) return;
    QDrag drag(this);
    drag.setMimeData(mime);
    drag.setPixmap(item->icon(0).pixmap(32, 32));
    drag.exec(Qt::MoveAction);
    // The document owns all nodes; never remove source items after QDrag returns.
}

bool BookmarkFolderTree::preview(const QMimeData* mime, const QPoint& position)
{
    auto* item = itemAt(position);
    const QString id = item ? item->data(0, BookmarkDragDrop::NodeIdRole).toString() : QString();
    const bool valid = !id.isEmpty() && handleDrop && handleDrop(mime, id, -1, false);
    dropFolderId_ = valid ? id : QString();
    dropRect_ = valid ? visualItemRect(item) : QRect();
    viewport()->update();
    return valid;
}

void BookmarkFolderTree::dragEnterEvent(QDragEnterEvent* event)
{
    BookmarkDragDrop::Payload payload;
    if ((event->possibleActions() & Qt::MoveAction) && BookmarkDragDrop::decode(event->mimeData(), &payload)) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    } else event->ignore();
}

void BookmarkFolderTree::dragMoveEvent(QDragMoveEvent* event)
{
    if ((event->possibleActions() & Qt::MoveAction) && preview(event->mimeData(), event->position().toPoint())) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    } else event->ignore();
}

void BookmarkFolderTree::clearDrop()
{
    dropFolderId_.clear();
    dropRect_ = {};
    viewport()->update();
}

void BookmarkFolderTree::dragLeaveEvent(QDragLeaveEvent* event)
{
    clearDrop();
    event->accept();
}

void BookmarkFolderTree::dropEvent(QDropEvent* event)
{
    if ((event->possibleActions() & Qt::MoveAction) && preview(event->mimeData(), event->position().toPoint())
        && handleDrop(event->mimeData(), dropFolderId_, -1, true)) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    } else event->ignore();
    clearDrop();
}

void BookmarkFolderTree::paintEvent(QPaintEvent* event)
{
    QTreeWidget::paintEvent(event);
    if (dropRect_.isEmpty()) return;
    QPainter painter(viewport());
    painter.setPen(QPen(palette().highlight().color(), 2));
    painter.drawRoundedRect(dropRect_.adjusted(1, 1, -2, -2), 3, 3);
}

BookmarkPathButton::BookmarkPathButton(QWidget* parent) : QToolButton(parent)
{
    setAcceptDrops(true);
    setAutoRaise(true);
}

void BookmarkPathButton::dragEnterEvent(QDragEnterEvent* event)
{
    if ((event->possibleActions() & Qt::MoveAction) && handleDrop && handleDrop(event->mimeData(), folderId, -1, false)) {
        setDown(true);
        event->setDropAction(Qt::MoveAction);
        event->accept();
    } else event->ignore();
}

void BookmarkPathButton::dragMoveEvent(QDragMoveEvent* event)
{
    const bool valid = (event->possibleActions() & Qt::MoveAction) && handleDrop
        && handleDrop(event->mimeData(), folderId, -1, false);
    setDown(valid);
    if (valid) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    } else event->ignore();
}

void BookmarkPathButton::dragLeaveEvent(QDragLeaveEvent* event)
{
    setDown(false);
    event->accept();
}

void BookmarkPathButton::dropEvent(QDropEvent* event)
{
    setDown(false);
    if ((event->possibleActions() & Qt::MoveAction) && handleDrop && handleDrop(event->mimeData(), folderId, -1, true)) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    } else event->ignore();
}
