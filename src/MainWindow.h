#pragma once

#include "BookmarkDocument.h"
#include "ChromeProfiles.h"
#include "HealthChecker.h"

#include <QHash>
#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QSplitter;
class QTableWidgetItem;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void reloadProfiles();
    void loadSelectedProfile();
    void openBookmarksFile();
    void saveBookmarks();
    void refreshList();
    void newFolder();
    void newBookmark();
    void renameSelected();
    void deleteSelected();
    void moveSelected();
    void openSelectedUrl();
    void checkUrls();
    void onHealthResult(BookmarkNode* node, const HealthResult& result);
    void onHealthFinished(int total, int failed);

private:
    BookmarkDocument document_;
    QVector<ChromeProfile> profiles_;
    HealthChecker health_;
    QHash<BookmarkNode*, HealthResult> healthResults_;

    QComboBox* profileCombo_ = nullptr;
    QTreeWidget* folderTree_ = nullptr;
    QTableWidget* itemTable_ = nullptr;
    QLineEdit* searchEdit_ = nullptr;
    QCheckBox* includeSubfolders_ = nullptr;
    QPushButton* checkButton_ = nullptr;

    void buildUi();
    void refreshTree();
    void addFolderItem(QTreeWidgetItem* parentItem, BookmarkNode* node);
    BookmarkNode* currentFolder() const;
    QVector<BookmarkNode*> selectedListNodes() const;
    QVector<BookmarkNode*> collectUrlNodes(BookmarkNode* folder, bool recursive) const;
    QVector<BookmarkNode*> collectFolders() const;
    BookmarkNode* chooseFolder();
    void setStatus(const QString& text);
    static BookmarkNode* nodeFromItem(QTreeWidgetItem* item);
    static BookmarkNode* nodeFromTableItem(QTableWidgetItem* item);
    static void setNodeData(QTreeWidgetItem* item, BookmarkNode* node);
    static void setNodeData(QTableWidgetItem* item, BookmarkNode* node);
};
