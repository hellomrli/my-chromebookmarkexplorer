#pragma once

#include "BookmarkDocument.h"
#include "ChromeProfiles.h"
#include "HealthChecker.h"
#include "Updater.h"

#include <QHash>
#include <QIcon>
#include <QMainWindow>
#include <QSet>
#include <QStringList>

class BookmarkIconView;
class FaviconLoader;
class BookmarkFolderTree;
class BookmarkPathButton;
class QStackedWidget;
class QHBoxLayout;
class QMimeData;
class QPoint;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLineEdit;
class QProgressDialog;
class QPushButton;
class QSpinBox;
class QTableWidgetItem;
class QTableWidget;
class QTreeWidgetItem;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr, bool backgroundServices = true);
    bool loadBookmarks(const QString& path, QString* error = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void reloadProfiles();
    void loadSelectedProfile();
    void openBookmarksFile();
    void saveBookmarks();
    void saveBookmarksAs();
    void exportBookmarks();
    void importBookmarks();
    void refreshList();
    void newFolder();
    void newBookmark();
    void renameSelected();
    void editSelectedUrl();
    void batchEditUrls();
    void editTags();
    void deleteSelected();
    void moveSelected();
    void openSelectedUrl();
    void checkUrls();
    void scanDuplicates();
    void deleteFailedUrls();
    void moveFailedUrls();
    void checkForUpdates();
    void onUpdateAvailable(const QString& version, const QString& url, const QString& notes);
    void onHealthResult(BookmarkNode* node, const HealthResult& result);
    void onHealthFinished(int total, int failed);
    void showTreeContextMenu(const QPoint& position);
    void showTableContextMenu(const QPoint& position);

private:
    BookmarkDocument document_;
    QVector<ChromeProfile> profiles_;
    HealthChecker health_;
    Updater updater_;
    QHash<BookmarkNode*, HealthResult> healthResults_;
    QHash<QString, QIcon> siteIcons_;
    FaviconLoader* faviconLoader_ = nullptr;
    bool startupLoading_ = true;
    bool treeOperation_ = false;
    int loadedProfileIndex_ = -1;
    int healthTotal_ = 0;
    int healthCompleted_ = 0;
    QString displayedFolderId_;

    QComboBox* profileCombo_ = nullptr;
    BookmarkFolderTree* folderTree_ = nullptr;
    QTableWidget* itemTable_ = nullptr;
    BookmarkIconView* iconView_ = nullptr;
    QStackedWidget* views_ = nullptr;
    BookmarkPathButton* upButton_ = nullptr;
    QHBoxLayout* breadcrumbLayout_ = nullptr;
    QLineEdit* searchEdit_ = nullptr;
    QCheckBox* includeSubfolders_ = nullptr;
    QSpinBox* concurrencySpin_ = nullptr;
    QSpinBox* timeoutSpin_ = nullptr;
    QPushButton* checkButton_ = nullptr;
    QProgressDialog* progressDialog_ = nullptr;

    void buildUi();
    void refreshTree(const QString& preferredId = {});
    void addFolderItem(QTreeWidgetItem* parentItem, BookmarkNode* node);
    void navigateTo(const QString& id);
    void updateNavigation();
    void switchView(int index);
    void restoreListSelection(const QStringList& ids);
    QStringList selectedListIds() const;
    QString currentFolderId() const;
    QIcon nodeIcon(const BookmarkNode* node);
    void updateSiteIcon(const QString& site, const QIcon& icon);
    QMimeData* createDrag(const QStringList& ids) const;
    bool handleDrop(const QMimeData* mime, const QString& targetId, int index, bool commit);
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
    void collectCheckedFolderNodes(QTreeWidgetItem* item, QVector<BookmarkNode*>* nodes) const;
    QVector<BookmarkNode*> filterNestedNodes(const QVector<BookmarkNode*>& nodes) const;
    QTreeWidgetItem* findFolderItemById(const QString& id) const;
    bool isDescendantOfAny(BookmarkNode* node, const QSet<BookmarkNode*>& candidates) const;
    void setStatus(const QString& text);
    void showProgress(const QString& title, const QString& label, int maximum);
    void updateProgress(const QString& label, int value);
    void closeProgress();
    BookmarkNode* nodeFromItem(QTreeWidgetItem* item) const;
    BookmarkNode* nodeFromTableItem(QTableWidgetItem* item) const;
    static void setNodeData(QTreeWidgetItem* item, BookmarkNode* node);
    static void setNodeData(QTableWidgetItem* item, BookmarkNode* node);
};
