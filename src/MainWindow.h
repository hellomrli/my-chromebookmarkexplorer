#pragma once

#include "BookmarkDocument.h"
#include "ChromeProfiles.h"
#include "HealthChecker.h"

#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QStringList>

class QPoint;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLineEdit;
class QProgressDialog;
class QPushButton;
class QSplitter;
class QSpinBox;
class QTableWidgetItem;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void reloadProfiles();
    void loadSelectedProfile();
    void openBookmarksFile();
    void saveBookmarks();
    void saveBookmarksAs();
    void refreshList();
    void newFolder();
    void newBookmark();
    void renameSelected();
    void editSelectedUrl();
    void deleteSelected();
    void moveSelected();
    void openSelectedUrl();
    void checkUrls();
    void scanDuplicates();
    void deleteFailedUrls();
    void moveFailedUrls();
    void onHealthResult(BookmarkNode* node, const HealthResult& result);
    void onHealthFinished(int total, int failed);
    void showTreeContextMenu(const QPoint& position);
    void showTableContextMenu(const QPoint& position);

private:
    BookmarkDocument document_;
    QVector<ChromeProfile> profiles_;
    HealthChecker health_;
    QHash<BookmarkNode*, HealthResult> healthResults_;
    bool startupLoading_ = true;
    int loadedProfileIndex_ = -1;
    int healthTotal_ = 0;
    int healthCompleted_ = 0;

    QComboBox* profileCombo_ = nullptr;
    QTreeWidget* folderTree_ = nullptr;
    QTableWidget* itemTable_ = nullptr;
    QLineEdit* searchEdit_ = nullptr;
    QCheckBox* includeSubfolders_ = nullptr;
    QSpinBox* concurrencySpin_ = nullptr;
    QPushButton* checkButton_ = nullptr;
    QProgressDialog* progressDialog_ = nullptr;

    void buildUi();
    void refreshTree(const QString& preferredPath = {});
    void addFolderItem(QTreeWidgetItem* parentItem, BookmarkNode* node);
    BookmarkNode* currentFolder() const;
    QVector<BookmarkNode*> selectedListNodes() const;
    QVector<BookmarkNode*> checkedFolderNodes() const;
    QVector<BookmarkNode*> selectedOperationNodes() const;
    QVector<BookmarkNode*> collectUrlNodes(BookmarkNode* folder, bool recursive) const;
    QVector<BookmarkNode*> collectFolders() const;
    BookmarkNode* chooseFolder();
    bool saveBookmarksInternal(bool forceChoosePath = false);
    bool confirmDiscardChanges();
    void updateDirtyState();
    QVector<BookmarkNode*> failedHealthNodes() const;
    QString currentFolderPath() const;
    QString nearestExistingFolderPath(QString path) const;
    QStringList checkedFolderPaths() const;
    void restoreCheckedFolders(const QStringList& paths);
    void collectCheckedFolderNodes(QTreeWidgetItem* item, QVector<BookmarkNode*>* nodes) const;
    void collectCheckedFolderPaths(QTreeWidgetItem* item, QStringList* paths) const;
    QVector<BookmarkNode*> filterNestedNodes(const QVector<BookmarkNode*>& nodes) const;
    QTreeWidgetItem* findFolderItemByPath(const QString& path) const;
    QTreeWidgetItem* findFolderItemRecursive(QTreeWidgetItem* item, const QString& path) const;
    bool isDescendantOfAny(BookmarkNode* node, const QSet<BookmarkNode*>& candidates) const;
    void setStatus(const QString& text);
    void showProgress(const QString& title, const QString& label, int maximum);
    void updateProgress(const QString& label, int value);
    void closeProgress();
    static BookmarkNode* nodeFromItem(QTreeWidgetItem* item);
    static BookmarkNode* nodeFromTableItem(QTableWidgetItem* item);
    static void setNodeData(QTreeWidgetItem* item, BookmarkNode* node);
    static void setNodeData(QTableWidgetItem* item, BookmarkNode* node);
};
