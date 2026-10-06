#include "MainWindow.h"
#include "BookmarkIconView.h"
#include "BookmarkDragDrop.h"
#include "FaviconLoader.h"

#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QLineEdit>
#include <QMimeData>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
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
