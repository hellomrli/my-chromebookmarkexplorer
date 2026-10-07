#include "MainWindow.h"
#include "BookmarkIconView.h"
#include "BookmarkDragDrop.h"
#include "FaviconLoader.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QHostAddress>
#include <QInputDialog>
#include <QLineEdit>
#include <QMimeData>
#include <QMessageBox>
#include <QPainter>
#include <QStandardPaths>
#include <QStyleOptionViewItem>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidgetItemIterator>
#include <memory>

class BookmarkExplorerTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir directory_;
    std::unique_ptr<MainWindow> window_;
    BookmarkIconView* icons_ = nullptr;
    BookmarkFolderTree* tree_ = nullptr;
    QString path_;

    QTreeWidgetItem* folder(const QString& id) const
    {
        for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
            if ((*it)->data(0, BookmarkDragDrop::NodeIdRole).toString() == id) return *it;
        }
        return nullptr;
    }

    QStringList visibleIds() const
    {
        QStringList ids;
        for (int i = 0; i < icons_->count(); ++i) ids << icons_->item(i)->data(BookmarkDragDrop::NodeIdRole).toString();
        return ids;
    }

    std::unique_ptr<QMimeData> drag(const QStringList& ids) const
    {
        return std::unique_ptr<QMimeData>(icons_->makeMime(ids));
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QVERIFY(directory_.isValid());
    }

    void init()
    {
        path_ = directory_.filePath(QStringLiteral("Bookmarks.json"));
        QFile file(path_);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"version\":1,\"roots\":{"
            "\"bookmark_bar\":{\"id\":\"1\",\"type\":\"folder\",\"name\":\"Bookmarks\",\"children\":["
            "{\"id\":\"10\",\"type\":\"url\",\"name\":\"Alpha\",\"url\":\"https://alpha.example/\"},"
            "{\"id\":\"11\",\"type\":\"folder\",\"name\":\"Work/Docs\",\"children\":["
            "{\"id\":\"20\",\"type\":\"folder\",\"name\":\"Nested\",\"children\":["
            "{\"id\":\"21\",\"type\":\"url\",\"name\":\"Nested\",\"url\":\"https://nested.example/\"}]}]},"
            "{\"id\":\"12\",\"type\":\"url\",\"name\":\"Beta\",\"url\":\"https://beta.example/\"},"
            "{\"id\":\"13\",\"type\":\"url\",\"name\":\"Gamma\",\"url\":\"https://gamma.example/\"},"
            "{\"id\":\"14\",\"type\":\"folder\",\"name\":\"Work/Docs\",\"children\":[]}]},"
            "\"other\":{\"id\":\"2\",\"type\":\"folder\",\"name\":\"其他书签\",\"children\":[]}}}");
        file.close();
        window_ = std::make_unique<MainWindow>(nullptr, false);
        QString error;
        QVERIFY2(window_->loadBookmarks(path_, &error), qPrintable(error));
        icons_ = static_cast<BookmarkIconView*>(window_->findChild<QListWidget*>(QStringLiteral("bookmarkIcons")));
        tree_ = static_cast<BookmarkFolderTree*>(window_->findChild<QTreeWidget*>(QStringLiteral("folderTree")));
        QVERIFY(icons_);
        QVERIFY(tree_);
        window_->resize(1000, 700);
        window_->show();
        QTest::qWait(20);
        QCOMPARE(icons_->count(), 5);
    }

    void cleanup() { window_.reset(); }

    void iconsAndNavigation()
    {
        QVERIFY(!QIcon(QStringLiteral(":/icons/folder.png")).isNull());
        for (int i = 0; i < icons_->count(); ++i) QVERIFY(!icons_->item(i)->icon().isNull());
        QCOMPARE(icons_->viewMode(), QListView::IconMode);
        tree_->setCurrentItem(folder(QStringLiteral("14")));
        QCOMPARE(icons_->count(), 0);
        auto* up = window_->findChild<QToolButton*>(QStringLiteral("upButton"));
        QVERIFY(up->isEnabled());
        QTest::mouseClick(up, Qt::LeftButton);
        QCOMPARE(icons_->count(), 5);
        icons_->setCurrentRow(1);
        icons_->setFocus();
        QTest::keyClick(icons_, Qt::Key_Return);
        QCOMPARE(tree_->currentItem()->data(0, BookmarkDragDrop::NodeIdRole).toString(), QStringLiteral("11"));
        QCOMPARE(visibleIds(), QStringList({QStringLiteral("20")}));
        QVERIFY(!window_->windowTitle().endsWith(QStringLiteral(" *")));
    }

    void faviconUpdatesBothViewsWithoutResettingSelection()
    {
        auto* loader = window_->findChild<FaviconLoader*>();
        auto* table = window_->findChild<QTableWidget*>(QStringLiteral("bookmarkDetails"));
        QVERIFY(loader);
        QVERIFY(table);
        icons_->item(0)->setSelected(true);
        icons_->item(2)->setSelected(true);
        table->item(2, 0)->setCheckState(Qt::Checked);
        const auto folderIcon = icons_->item(1)->icon().cacheKey();
        QPixmap pixmap(48, 48);
        pixmap.fill(Qt::red);
        const QIcon favicon(pixmap);
        loader->iconReady(QStringLiteral("https://alpha.example/"), favicon);
        QCOMPARE(icons_->item(0)->icon().cacheKey(), favicon.cacheKey());
        QCOMPARE(table->item(0, 1)->icon().cacheKey(), favicon.cacheKey());
        QCOMPARE(icons_->item(1)->icon().cacheKey(), folderIcon);
        QCOMPARE(icons_->selectedNodeIds(), QStringList({QStringLiteral("10"), QStringLiteral("12")}));
        QCOMPARE(table->item(2, 0)->checkState(), Qt::Checked);
        QVERIFY(!window_->windowTitle().endsWith(QStringLiteral(" *")));
        tree_->setCurrentItem(folder(QStringLiteral("14")));
        loader->iconReady(QStringLiteral("https://alpha.example/"), favicon);
        QCOMPARE(icons_->count(), 0);
        QCOMPARE(table->rowCount(), 0);
    }

    void savesReorderedBookmarksFromIconView()
    {
        auto mime = drag({QStringLiteral("13"), QStringLiteral("10")});
        QVERIFY(icons_->handleDrop(mime.get(), QStringLiteral("1"), 2, true));
        QTRY_COMPARE(visibleIds(), QStringList({QStringLiteral("11"), QStringLiteral("10"),
            QStringLiteral("13"), QStringLiteral("12"), QStringLiteral("14")}));
        const auto expected = visibleIds();
        QVERIFY(window_->windowTitle().endsWith(QStringLiteral(" *")));
        auto* save = window_->findChild<QAction*>(QStringLiteral("saveBookmarks"));
        QVERIFY(save);
        QCOMPARE(save->shortcut(), QKeySequence(QKeySequence::Save));
        save->trigger();
        QVERIFY(!window_->windowTitle().endsWith(QStringLiteral(" *")));
        BookmarkDocument saved;
        QVERIFY(saved.load(path_));
        QStringList actual;
        for (const auto& child : saved.nodeById(QStringLiteral("1"))->children) actual.append(child->id());
        QCOMPARE(actual, expected);
        QVERIFY(!QDir(directory_.path()).entryList({QStringLiteral("Bookmarks.json.backup-*")}).isEmpty());
        QVERIFY(window_->loadBookmarks(path_));
        QCOMPARE(visibleIds(), expected);
    }

    void failedSaveCanRecoverToIndependentCopy()
    {
        auto mime = drag({QStringLiteral("13"), QStringLiteral("10")});
        QVERIFY(icons_->handleDrop(mime.get(), QStringLiteral("1"), 2, true));
        QTRY_COMPARE(visibleIds().first(), QStringLiteral("11"));
        const auto expected = visibleIds();
        const QByteArray external = "external changes must survive";
        QFile source(path_);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write(external), external.size());
        source.close();
        const QString copy = directory_.filePath(QStringLiteral("Recovered.json"));
        bool offeredSaveAs = false;
        bool selectedCopy = false;
        int attempts = 0;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            auto* modal = QApplication::activeModalWidget();
            if (++attempts > 200) {
                if (auto* dialog = qobject_cast<QDialog*>(modal)) dialog->reject();
                return;
            }
            if (auto* message = qobject_cast<QMessageBox*>(modal)) {
                for (auto* button : message->buttons()) {
                    if (button->text().startsWith(QStringLiteral("另存为副本"))) {
                        offeredSaveAs = true;
                        button->click();
                        return;
                    }
                }
                message->reject();
            } else if (auto* dialog = qobject_cast<QFileDialog*>(modal); dialog && !selectedCopy) {
                dialog->setDirectory(directory_.path());
                selectedCopy = true;
                QTimer::singleShot(50, dialog, [dialog, copy] {
                    auto* filename = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
                    if (filename) filename->setText(copy);
                    QMetaObject::invokeMethod(dialog, "accept");
                });
            }
        });
        responder.start(10);
        window_->findChild<QAction*>(QStringLiteral("saveBookmarks"))->trigger();
        responder.stop();
        QVERIFY(offeredSaveAs);
        QVERIFY(selectedCopy);
        QVERIFY(attempts <= 200);
        QVERIFY(!window_->windowTitle().endsWith(QStringLiteral(" *")));
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), external);
        BookmarkDocument recovered;
        QVERIFY(recovered.load(copy));
        QStringList actual;
        for (const auto& child : recovered.nodeById("1")->children) actual.append(child->id());
        QCOMPARE(actual, expected);
    }

    void healthBadgeIsPaintedBelowTheLogo()
    {
        auto* item = icons_->item(0);
        QStyleOptionViewItem option;
        option.initFrom(icons_);
        option.rect = QRect(0, 0, 120, 128);
        option.decorationSize = icons_->iconSize();
        option.decorationPosition = QStyleOptionViewItem::Top;
        const auto render = [&] {
            QImage image(option.rect.size(), QImage::Format_ARGB32);
            image.fill(Qt::white);
            QPainter painter(&image);
            icons_->itemDelegate()->paint(&painter, option, icons_->model()->index(0, 0));
            painter.end();
            return image;
        };
        item->setData(BookmarkIconView::HealthTextRole, QString());
        const auto withoutBadge = render();
        item->setData(BookmarkIconView::HealthTextRole, QStringLiteral("正常 · 200"));
        item->setData(BookmarkIconView::HealthColorRole, QColor("#15803d"));
        const auto withBadge = render();
        QCOMPARE(withoutBadge.copy(0, 0, 120, 104), withBadge.copy(0, 0, 120, 104));
        QVERIFY(withoutBadge.copy(0, 104, 120, 24) != withBadge.copy(0, 104, 120, 24));
    }

    void healthResultsAreVisibleWithoutReplacingIconsOrSelection()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [&server] {
            while (auto* socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                    const QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", request);
                    if (!request.contains("\r\n\r\n")) return;
                    socket->disconnect(socket, &QTcpSocket::readyRead, nullptr, nullptr);
                    const QByteArray path = request.split(' ').value(1);
                    const QByteArray status = path == "/missing" ? "404 Not Found"
                        : (path == "/restricted" ? "403 Forbidden" : "200 OK");
                    QTimer::singleShot(40, socket, [socket, status] {
                        socket->write("HTTP/1.1 " + status + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                        socket->disconnectFromHost();
                    });
                });
            }
        });
        const QString base = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        BookmarkDocument document;
        QVERIFY(document.load(path_));
        QVERIFY(document.updateUrl(document.nodeById("10"), base + QStringLiteral("/ok")));
        QVERIFY(document.updateUrl(document.nodeById("12"), base + QStringLiteral("/missing")));
        QVERIFY(document.updateUrl(document.nodeById("13"), base + QStringLiteral("/restricted")));
        QVERIFY(document.save({}, false));
        QVERIFY(window_->loadBookmarks(path_));
        window_->findChild<QCheckBox*>()->setChecked(false);
        auto* table = window_->findChild<QTableWidget*>(QStringLiteral("bookmarkDetails"));
        auto* first = icons_->item(0);
        QCOMPARE(first->data(BookmarkIconView::HealthTextRole).toString(), QStringLiteral("未检测"));
        first->setSelected(true);
        icons_->item(2)->setSelected(true);
        table->item(3, 0)->setCheckState(Qt::Checked);
        QPixmap pixmap(48, 48);
        pixmap.fill(Qt::magenta);
        const QIcon favicon(pixmap);
        auto* loader = window_->findChild<FaviconLoader*>();
        loader->iconReady(FaviconLoader::siteKey(base), favicon);
        QVERIFY(QMetaObject::invokeMethod(window_.get(), "checkUrls"));
        QCOMPARE(first->data(BookmarkIconView::HealthTextRole).toString(), QStringLiteral("检测中…"));
        QTRY_COMPARE(table->item(0, 6)->text(), QStringLiteral("200"));
        QTRY_COMPARE(table->item(2, 6)->text(), QStringLiteral("404"));
        QTRY_COMPARE(table->item(3, 6)->text(), QStringLiteral("403"));
        QCOMPARE(icons_->item(0), first);
        QVERIFY(first->data(BookmarkIconView::HealthTextRole).toString().contains(QStringLiteral("200")));
        QCOMPARE(first->data(BookmarkIconView::HealthColorRole).value<QColor>(), QColor("#15803d"));
        QCOMPARE(icons_->item(2)->data(BookmarkIconView::HealthColorRole).value<QColor>(), QColor("#dc2626"));
        QCOMPARE(icons_->item(3)->data(BookmarkIconView::HealthColorRole).value<QColor>(), QColor("#b45309"));
        QVERIFY(icons_->item(1)->data(BookmarkIconView::HealthTextRole).toString().isEmpty());
        QCOMPARE(first->icon().cacheKey(), favicon.cacheKey());
        QCOMPARE(icons_->selectedNodeIds(), QStringList({QStringLiteral("10"), QStringLiteral("12")}));
        QCOMPARE(table->item(3, 0)->checkState(), Qt::Checked);
        QVERIFY(!window_->windowTitle().endsWith(QStringLiteral(" *")));
        loader->iconReady(FaviconLoader::siteKey(base), favicon);
        QVERIFY(icons_->item(2)->data(BookmarkIconView::HealthTextRole).toString().contains(QStringLiteral("404")));
        QVERIFY(QMetaObject::invokeMethod(window_.get(), "refreshList"));
        QVERIFY(icons_->item(2)->data(BookmarkIconView::HealthTextRole).toString().contains(QStringLiteral("404")));
        QVERIFY(QMetaObject::invokeMethod(window_.get(), "checkUrls"));
        QCOMPARE(icons_->item(2)->data(BookmarkIconView::HealthTextRole).toString(), QStringLiteral("检测中…"));
        QVERIFY(table->item(2, 6)->text().isEmpty());
        QTRY_COMPARE(table->item(0, 6)->text(), QStringLiteral("200"));
        QTRY_COMPARE(table->item(2, 6)->text(), QStringLiteral("404"));
        QTRY_COMPARE(table->item(3, 6)->text(), QStringLiteral("403"));
        icons_->clearSelection();
        icons_->item(0)->setSelected(true);
        QTimer::singleShot(0, this, [&] {
            auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            if (dialog) {
                dialog->setTextValue(base + QStringLiteral("/changed"));
                dialog->accept();
            }
        });
        QVERIFY(QMetaObject::invokeMethod(window_.get(), "editSelectedUrl"));
        QCOMPARE(icons_->item(0)->data(BookmarkIconView::HealthTextRole).toString(), QStringLiteral("未检测"));
        QVERIFY(icons_->item(2)->data(BookmarkIconView::HealthTextRole).toString().contains(QStringLiteral("404")));
    }

    void doubleClickFolder()
    {
        const auto position = icons_->visualItemRect(icons_->item(1)).center();
        QTest::mouseClick(icons_->viewport(), Qt::LeftButton, Qt::NoModifier, position);
        QTest::mouseDClick(icons_->viewport(), Qt::LeftButton, Qt::NoModifier, position);
        QTRY_COMPARE(tree_->currentItem()->data(0, BookmarkDragDrop::NodeIdRole).toString(), QStringLiteral("11"));
    }

    void selectionSurvivesViewSwitch()
    {
        icons_->item(0)->setSelected(true);
        icons_->item(2)->setSelected(true);
        window_->findChild<QAction*>(QStringLiteral("detailMode"))->trigger();
        auto* table = window_->findChild<QTableWidget*>(QStringLiteral("bookmarkDetails"));
        QVERIFY(table->selectionModel()->isRowSelected(0));
        QVERIFY(table->selectionModel()->isRowSelected(2));
        table->clearSelection();
        table->item(4, 0)->setCheckState(Qt::Checked);
        window_->findChild<QAction*>(QStringLiteral("iconMode"))->trigger();
        QCOMPARE(icons_->selectedNodeIds(), QStringList({QStringLiteral("14")}));
        QVERIFY(!window_->windowTitle().endsWith(QStringLiteral(" *")));
    }

    void batchReorderAndStalePayload()
    {
        auto mime = drag({QStringLiteral("13"), QStringLiteral("10")});
        QVERIFY(mime);
        QVERIFY(icons_->handleDrop(mime.get(), QStringLiteral("1"), 2, false));
        QVERIFY(icons_->handleDrop(mime.get(), QStringLiteral("1"), 2, true));
        QTRY_COMPARE(visibleIds(), QStringList({"11", "10", "13", "12", "14"}));
        QCOMPARE(icons_->selectedNodeIds(), QStringList({"10", "13"}));
        QVERIFY(window_->windowTitle().endsWith(QStringLiteral(" *")));
        QVERIFY(!icons_->handleDrop(mime.get(), QStringLiteral("2"), -1, true));
        QFile source(path_);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QVERIFY(source.readAll().contains("Alpha"));
    }

    void dropIntoFolderEvent()
    {
        auto mime = drag({QStringLiteral("10"), QStringLiteral("12")});
        QVERIFY(mime);
        BookmarkDragDrop::Payload payload;
        QVERIFY(BookmarkDragDrop::decode(mime.get(), &payload));
        QVERIFY(icons_->viewport()->acceptDrops());
        QVERIFY(icons_->isVisible());
        const QPoint position = icons_->visualItemRect(icons_->item(1)).center();
        QDragEnterEvent enter(position, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(icons_->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(position, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(icons_->viewport(), &drop);
        QVERIFY(drop.isAccepted());
        QTRY_COMPARE(visibleIds(), QStringList({"11", "13", "14"}));
        tree_->setCurrentItem(folder(QStringLiteral("11")));
        QCOMPARE(visibleIds(), QStringList({"20", "10", "12"}));
    }

    void treeAndParentDrops()
    {
        auto mime = drag({QStringLiteral("10")});
        const QPoint position = tree_->visualItemRect(folder(QStringLiteral("14"))).center();
        QDragEnterEvent enter(position, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(tree_->viewport(), &enter);
        QDropEvent drop(position, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(tree_->viewport(), &drop);
        QVERIFY(drop.isAccepted());
        QTRY_COMPARE(icons_->count(), 4);
        tree_->setCurrentItem(folder(QStringLiteral("14")));
        QCOMPARE(visibleIds(), QStringList({"10"}));
        auto parentMime = drag({QStringLiteral("10")});
        auto* up = static_cast<BookmarkPathButton*>(window_->findChild<QToolButton*>(QStringLiteral("upButton")));
        QDragEnterEvent parentEnter(QPoint(5, 5), Qt::MoveAction, parentMime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(up, &parentEnter);
        QDropEvent parentDrop(QPoint(5, 5), Qt::MoveAction, parentMime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(up, &parentDrop);
        QVERIFY(parentDrop.isAccepted());
        QTRY_COMPARE(icons_->count(), 0);
        tree_->setCurrentItem(folder(QStringLiteral("1")));
        QCOMPARE(visibleIds().last(), QStringLiteral("10"));
    }

    void rejectsInvalidMovesAndExternalData()
    {
        auto mime = drag({QStringLiteral("11")});
        QVERIFY(!icons_->handleDrop(mime.get(), QStringLiteral("11"), -1, true));
        QVERIFY(!icons_->handleDrop(mime.get(), QStringLiteral("20"), -1, true));
        QVERIFY(!icons_->handleDrop(mime.get(), QStringLiteral("10"), -1, true));
        QVERIFY(!drag({QStringLiteral("1")}));
        QMimeData external;
        external.setText(QStringLiteral("https://example.com/"));
        QVERIFY(!icons_->handleDrop(&external, QStringLiteral("1"), -1, true));
        QVERIFY(!window_->windowTitle().endsWith(QStringLiteral(" *")));
        QCOMPARE(icons_->count(), 5);
    }

    void filteredMovesButNotReorders()
    {
        auto* search = window_->findChild<QLineEdit*>(QStringLiteral("bookmarkSearch"));
        search->setText(QStringLiteral("Alpha"));
        QCOMPARE(visibleIds(), QStringList({"10"}));
        auto mime = drag({QStringLiteral("10")});
        QVERIFY(!icons_->handleDrop(mime.get(), QStringLiteral("1"), 0, true));
        QVERIFY(!icons_->dropLocation(icons_->visualItemRect(icons_->item(0)).center()).valid());
        QVERIFY(icons_->handleDrop(mime.get(), QStringLiteral("14"), -1, true));
        QTRY_COMPARE(icons_->count(), 0);
        search->clear();
        tree_->setCurrentItem(folder(QStringLiteral("14")));
        QCOMPARE(visibleIds(), QStringList({"10"}));
    }

    void filteredFolderAppend_data()
    {
        QTest::addColumn<bool>("treeTarget");
        QTest::newRow("tree") << true;
        QTest::newRow("breadcrumb") << false;
    }

    void filteredFolderAppend()
    {
        QFETCH(bool, treeTarget);
        auto* search = window_->findChild<QLineEdit*>(QStringLiteral("bookmarkSearch"));
        search->setText(QStringLiteral("Nested"));
        QVERIFY(visibleIds().isEmpty());
        auto mime = drag({QStringLiteral("20")});
        QVERIFY(mime);
        QVERIFY(icons_->handleDrop(mime.get(), QStringLiteral("1"), -1, false));
        QVERIFY(!window_->windowTitle().endsWith(QStringLiteral(" *")));

        QWidget* receiver = nullptr;
        QPoint position;
        if (treeTarget) {
            receiver = tree_->viewport();
            position = tree_->visualItemRect(folder(QStringLiteral("1"))).center();
        } else {
            for (auto* button : window_->findChildren<QToolButton*>()) {
                auto* path = dynamic_cast<BookmarkPathButton*>(button);
                if (path && path->isVisible() && path->folderId == QStringLiteral("1")) {
                    receiver = path;
                    position = path->rect().center();
                    break;
                }
            }
        }
        QVERIFY(receiver);
        QDragEnterEvent enter(position, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(receiver, &enter);
        QVERIFY(enter.isAccepted());
        QDragMoveEvent move(position, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(receiver, &move);
        QVERIFY(move.isAccepted());
        QDropEvent drop(position, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(receiver, &drop);
        QVERIFY(drop.isAccepted());
        QTRY_COMPARE(visibleIds(), QStringList({"20"}));
        QVERIFY(window_->windowTitle().endsWith(QStringLiteral(" *")));
        QVERIFY(!icons_->handleDrop(mime.get(), QStringLiteral("2"), -1, false));
        QVERIFY(!icons_->handleDrop(mime.get(), QStringLiteral("2"), -1, true));

        search->clear();
        QCOMPARE(visibleIds(), QStringList({"10", "11", "12", "13", "14", "20"}));
        tree_->setCurrentItem(folder(QStringLiteral("11")));
        QVERIFY(visibleIds().isEmpty());
    }

    void filteredAppendRejectsReordersAtomically()
    {
        auto* search = window_->findChild<QLineEdit*>(QStringLiteral("bookmarkSearch"));
        search->setText(QStringLiteral("Alpha"));
        const QVector<QStringList> batches = {{"10"}, {"20", "10"}, {"10", "20"}};
        for (const auto& ids : batches) {
            auto mime = drag(ids);
            QVERIFY(mime);
            QVERIFY(!icons_->handleDrop(mime.get(), QStringLiteral("1"), -1, false));
            QVERIFY(!icons_->handleDrop(mime.get(), QStringLiteral("1"), -1, true));
        }
        auto incoming = drag({QStringLiteral("20")});
        QVERIFY(!icons_->handleDrop(incoming.get(), QStringLiteral("1"), 0, false));
        QVERIFY(!icons_->handleDrop(incoming.get(), QStringLiteral("1"), 0, true));
        const QRect rect = icons_->visualItemRect(icons_->item(0));
        QVERIFY(!icons_->dropLocation(QPoint(rect.left() + 1, rect.center().y())).valid());
        QVERIFY(!icons_->dropLocation(icons_->viewport()->rect().bottomRight() - QPoint(5, 5)).valid());
        QVERIFY(!window_->windowTitle().endsWith(QStringLiteral(" *")));
        QCOMPARE(visibleIds(), QStringList({"10"}));
        search->clear();
        QCOMPARE(visibleIds(), QStringList({"10", "11", "12", "13", "14"}));
        tree_->setCurrentItem(folder(QStringLiteral("11")));
        QCOMPARE(visibleIds(), QStringList({"20"}));
        QVERIFY(icons_->handleDrop(incoming.get(), QStringLiteral("2"), -1, false));
    }

    void noOpDoesNotDirty()
    {
        auto mime = drag({QStringLiteral("10")});
        QVERIFY(icons_->handleDrop(mime.get(), QStringLiteral("1"), 1, true));
        QVERIFY(!window_->windowTitle().endsWith(QStringLiteral(" *")));
        QVERIFY(icons_->handleDrop(mime.get(), QStringLiteral("1"), 0, false));
    }

    void documentSwitchInvalidatesDrag()
    {
        auto mime = drag({QStringLiteral("10")});
        tree_->setCurrentItem(folder(QStringLiteral("20")));
        QString error;
        QVERIFY2(window_->loadBookmarks(path_, &error), qPrintable(error));
        QVERIFY(!icons_->handleDrop(mime.get(), QStringLiteral("14"), -1, true));
        QCOMPARE(icons_->count(), 5);
        QVERIFY(!window_->loadBookmarks(directory_.filePath(QStringLiteral("missing")), &error));
        QCOMPARE(icons_->count(), 5);
    }

    void wrappedGridHitTesting()
    {
        window_->resize(540, 640);
        QTest::qWait(20);
        for (int i = 0; i < icons_->count(); ++i) {
            const QRect rect = icons_->visualItemRect(icons_->item(i));
            auto location = icons_->dropLocation(QPoint(rect.left() + 1, rect.center().y()));
            QVERIFY(location.valid());
            QCOMPARE(location.index, i);
            location = icons_->dropLocation(QPoint(rect.right() - 1, rect.center().y()));
            QCOMPARE(location.index, i + 1);
            if (icons_->item(i)->data(BookmarkDragDrop::FolderRole).toBool()) {
                location = icons_->dropLocation(rect.center());
                QVERIFY(location.intoFolder);
                QCOMPARE(location.folderId, icons_->item(i)->data(BookmarkDragDrop::NodeIdRole).toString());
            }
        }
    }
};

QTEST_MAIN(BookmarkExplorerTest)
#include "BookmarkExplorerTest.moc"
