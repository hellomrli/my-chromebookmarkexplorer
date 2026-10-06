#include "MainWindow.h"
#include "BookmarkIconView.h"
#include "BookmarkDragDrop.h"
#include "BatchEditDialog.h"
#include "ImportExport.h"
#include "Logger.h"
#include "TagDialog.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPoint>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <QAbstractItemView>
#include <QIcon>
#include <QStyle>
#include <QActionGroup>
#include <QCryptographicHash>
#include <QHBoxLayout>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTreeWidgetItemIterator>

#include <algorithm>

namespace {
constexpr int NodeRole = BookmarkDragDrop::NodeIdRole;

QString codeText(int code)
{
    return code > 0 ? QString::number(code) : QString();
}

QString healthResultTooltip(const HealthResult& result)
{
    if (result.status.isEmpty()) {
        return {};
    }

    QStringList lines;
    lines << QStringLiteral("状态：%1").arg(result.status);
    if (result.code > 0) {
        lines << QStringLiteral("HTTP：%1").arg(result.code);
    }
    if (result.attempts > 1) {
        lines << QStringLiteral("尝试次数：%1").arg(result.attempts);
    }
    if (!result.finalUrl.isEmpty() && result.finalUrl != result.url) {
        lines << QStringLiteral("最终网址：%1").arg(result.finalUrl);
    }
    if (!result.error.isEmpty()) {
        lines << QStringLiteral("详情：%1").arg(result.error);
    }
    return lines.join(QLatin1Char('\n'));
}

QString normalizedUrlKey(const QString& input)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QUrl url = QUrl::fromUserInput(trimmed);
    if (!url.isValid()) {
        return trimmed.toCaseFolded();
    }

    url.setFragment(QString());
    url.setScheme(url.scheme().toLower());
    url.setHost(url.host().toLower());
    QString path = url.path();
    while (path.size() > 1 && path.endsWith('/')) {
        path.chop(1);
    }
    url.setPath(path);
    return url.toString(QUrl::RemovePassword | QUrl::NormalizePathSegments).toCaseFolded();
}
}

MainWindow::MainWindow(QWidget* parent, bool backgroundServices)
    : QMainWindow(parent)
{
    // 测试窗口不扫描真实 Profile，也不进行网络更新检查。
    if (backgroundServices) {
        const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(logDir);
        const QString logFile = logDir + QStringLiteral("/ChromeBookmarkExplorer.log");
        Logger::instance().setLogFile(logFile);
        Logger::instance().setLevel(Logger::Level::Info);
        LOG_INFO(QStringLiteral("Application started"));
    }

    buildUi();
    connect(&health_, &HealthChecker::resultReady, this, &MainWindow::onHealthResult);
    connect(&health_, &HealthChecker::finished, this, &MainWindow::onHealthFinished);
    connect(&updater_, &Updater::updateAvailable, this, &MainWindow::onUpdateAvailable);
    connect(&updater_, &Updater::checkFailed, this, [this](const QString& error) {
        LOG_WARNING(QStringLiteral("Update check failed: %1").arg(error));
        setStatus(QStringLiteral("检查更新失败: %1").arg(error));
    });
    if (backgroundServices) {
        reloadProfiles();
        QTimer::singleShot(2000, &updater_, &Updater::checkForUpdates);
    }
}

void MainWindow::reloadProfiles()
{
    const bool showDialog = !startupLoading_;
    if (showDialog) {
        showProgress(QStringLiteral("刷新"), QStringLiteral("正在扫描 Chrome Profile..."), 2);
        updateProgress(QStringLiteral("正在扫描 Chrome Profile..."), 1);
    }

    profiles_ = discoverChromeProfiles();
    profileCombo_->clear();
    for (const auto& profile : profiles_) {
        profileCombo_->addItem(profile.label());
    }
    setStatus(profiles_.isEmpty()
        ? QStringLiteral("未发现 Chrome Profile，可手动打开 Bookmarks 文件")
        : QStringLiteral("发现 %1 个 Chrome Profile").arg(profiles_.size()));

    if (showDialog) {
        updateProgress(
            profiles_.isEmpty()
                ? QStringLiteral("未发现 Chrome Profile")
                : QStringLiteral("已发现 %1 个 Chrome Profile").arg(profiles_.size()),
            2);
        closeProgress();
    }
    startupLoading_ = false;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (confirmDiscardChanges()) {
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::loadSelectedProfile()
{
    const int index = profileCombo_->currentIndex();
    if (index < 0 || index >= profiles_.size()) {
        return;
    }
    if (!confirmDiscardChanges()) {
        QSignalBlocker blocker(profileCombo_);
        profileCombo_->setCurrentIndex(loadedProfileIndex_);
        return;
    }
    QString error;
    if (!loadBookmarks(profiles_[index].bookmarksPath, &error)) {
        QSignalBlocker blocker(profileCombo_);
        profileCombo_->setCurrentIndex(loadedProfileIndex_);
        LOG_ERROR(QStringLiteral("Failed to load profile: %1, error: %2").arg(profiles_[index].label(), error));
        QMessageBox::critical(this, QStringLiteral("打开失败"), error);
        return;
    }
    loadedProfileIndex_ = index;
    healthResults_.clear();
    refreshTree();
    setStatus(QStringLiteral("已打开：%1").arg(document_.path()));
}

void MainWindow::openBookmarksFile()
{
    if (!confirmDiscardChanges()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择 Chrome Bookmarks 文件"),
        QString(),
        QStringLiteral("Chrome Bookmarks (Bookmarks);;JSON (*.json);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!loadBookmarks(path, &error)) {
        LOG_ERROR(QStringLiteral("Failed to open bookmarks file: %1, error: %2").arg(path, error));
        QMessageBox::critical(this, QStringLiteral("打开失败"), error);
        return;
    }
    loadedProfileIndex_ = -1;
    {
        QSignalBlocker blocker(profileCombo_);
        profileCombo_->setCurrentIndex(-1);
    }
    healthResults_.clear();
    refreshTree();
    LOG_INFO(QStringLiteral("Opened bookmarks file: %1").arg(path));
    setStatus(QStringLiteral("已打开：%1").arg(document_.path()));
}

void MainWindow::saveBookmarks()
{
    saveBookmarksInternal(false);
}

void MainWindow::saveBookmarksAs()
{
    saveBookmarksInternal(true);
}

void MainWindow::exportBookmarks()
{
    if (document_.roots().empty()) {
        QMessageBox::information(this, QStringLiteral("导出"), QStringLiteral("没有可导出的书签"));
        return;
    }

    const QString filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出书签"),
        QStringLiteral("bookmarks"),
        QStringLiteral("HTML 书签文件 (*.html);;JSON 文件 (*.json);;CSV 文件 (*.csv);;所有文件 (*.*)"));

    if (filePath.isEmpty()) {
        return;
    }

    QString error;
    bool success = false;

    if (filePath.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive)) {
        success = ImportExport::exportToHtml(document_.roots(), filePath, &error);
    } else if (filePath.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        success = ImportExport::exportToJson(document_.roots(), filePath, &error);
    } else if (filePath.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) {
        success = ImportExport::exportToCsv(document_.roots(), filePath, &error);
    } else {
        error = QStringLiteral("不支持的文件格式");
    }

    if (success) {
        QMessageBox::information(this, QStringLiteral("导出"), QStringLiteral("导出成功！"));
        LOG_INFO(QStringLiteral("Exported bookmarks to: %1").arg(filePath));
        setStatus(QStringLiteral("已导出到：%1").arg(filePath));
    } else {
        LOG_ERROR(QStringLiteral("Failed to export bookmarks: %1").arg(error));
        QMessageBox::critical(this, QStringLiteral("导出失败"), error);
    }
}

void MainWindow::importBookmarks()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("导入书签"),
        QString(),
        QStringLiteral("HTML 书签文件 (*.html);;JSON 文件 (*.json);;所有文件 (*.*)"));

    if (filePath.isEmpty()) {
        return;
    }

    QVector<BookmarkNode*> importedRoots;
    QString error;
    bool success = false;

    if (filePath.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive)) {
        success = ImportExport::importFromHtml(filePath, importedRoots, &error);
    } else if (filePath.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        success = ImportExport::importFromJson(filePath, importedRoots, &error);
    } else {
        error = QStringLiteral("不支持的文件格式");
    }

    if (!success) {
        LOG_ERROR(QStringLiteral("Failed to import bookmarks: %1").arg(error));
        QMessageBox::critical(this, QStringLiteral("导入失败"), error);
        return;
    }

    // 将导入的节点添加到当前文档
    auto* targetFolder = currentFolder();
    if (!targetFolder) {
        LOG_WARNING(QStringLiteral("Import failed: no target folder selected"));
        QMessageBox::warning(this, QStringLiteral("导入"), QStringLiteral("请先选择一个文件夹作为导入目标"));
        for (auto* node : importedRoots) {
            delete node;
        }
        return;
    }

    int imported = 0;
    for (auto* root : importedRoots) {
        for (auto& child : root->children) {
            if (document_.add(child.get(), targetFolder, &error)) {
                ++imported;
            }
        }
        delete root;
    }

    refreshTree(currentFolderId());
    LOG_INFO(QStringLiteral("Imported %1 items from: %2").arg(imported).arg(filePath));
    QMessageBox::information(this, QStringLiteral("导入"), QStringLiteral("成功导入 %1 个项目").arg(imported));
    setStatus(QStringLiteral("已导入 %1 个项目").arg(imported));
}

bool MainWindow::saveBookmarksInternal(bool forceChoosePath)
{
    QString target = document_.path();
    if (forceChoosePath || target.isEmpty()) {
        target = QFileDialog::getSaveFileName(
            this,
            forceChoosePath ? QStringLiteral("另存为 Bookmarks 文件") : QStringLiteral("保存 Bookmarks 文件"),
            target.isEmpty() ? QStringLiteral("Bookmarks") : target,
            QStringLiteral("Chrome Bookmarks (Bookmarks);;JSON (*.json);;All files (*.*)"));
        if (target.isEmpty()) {
            return false;
        }
    }

    showProgress(QStringLiteral("保存"), QStringLiteral("准备保存收藏夹..."), 3);
    updateProgress(QStringLiteral("正在检查 Chrome 状态..."), 1);
    QString error;
    updateProgress(QStringLiteral("正在备份并写入 Bookmarks 文件..."), 2);
    if (!document_.save(target, true, &error)) {
        closeProgress();
        LOG_ERROR(QStringLiteral("Failed to save bookmarks to: %1, error: %2").arg(target, error));
        QMessageBox::critical(this, QStringLiteral("保存失败"), error);
        return false;
    }
    updateProgress(QStringLiteral("保存完成"), 3);
    closeProgress();
    updateDirtyState();
    LOG_INFO(QStringLiteral("Saved bookmarks to: %1").arg(target));
    setStatus(QStringLiteral("已保存并自动备份：%1").arg(target));
    return true;
}

bool MainWindow::loadBookmarks(const QString& path, QString* error)
{
    if (health_.isRunning()) {
        if (error) *error = QStringLiteral("请等待网址测活完成后再切换文件");
        return false;
    }
    if (!document_.load(path, error)) return false;
    {
        QSignalBlocker blocker(folderTree_);
        folderTree_->clear();
    }
    itemTable_->setRowCount(0);
    iconView_->clear();
    displayedFolderId_.clear();
    searchEdit_->clear();
    healthResults_.clear();
    siteIcons_.clear();
    refreshTree();
    return true;
}

void MainWindow::refreshList()
{
    auto* folder = currentFolder();
    const QString folderId = folder ? folder->id() : QString();
    const bool sameFolder = folderId == displayedFolderId_;
    const auto selected = sameFolder ? selectedListIds() : QStringList();
    const int iconScroll = sameFolder ? iconView_->verticalScrollBar()->value() : 0;
    const int tableScroll = sameFolder ? itemTable_->verticalScrollBar()->value() : 0;
    itemTable_->setRowCount(0);
    iconView_->clear();
    displayedFolderId_ = folderId;
    const QString query = searchEdit_->text().trimmed();
    iconView_->setFolderContext(folderId, !query.isEmpty());
    iconView_->setEmptyText(folder
        ? (query.isEmpty() ? QStringLiteral("这个文件夹是空的\n拖入书签，或右键新建文件夹 / 书签")
                           : QStringLiteral("没有匹配的书签\n清除搜索后可拖动调整顺序"))
        : QStringLiteral("选择 Chrome Profile 或打开 Bookmarks 文件\n以文件夹和图标整理你的书签"));
    if (!sameFolder) updateNavigation();
    if (!folder) return;

    QVector<BookmarkNode*> visibleNodes;
    for (int index = 0; index < static_cast<int>(folder->children.size()); ++index) {
        auto* node = folder->children[index].get();
        if (!query.isEmpty() && !node->name().contains(query, Qt::CaseInsensitive)
            && !node->url().contains(query, Qt::CaseInsensitive)) continue;
        visibleNodes.append(node);
        auto* item = new QListWidgetItem(nodeIcon(node), node->name(), iconView_);
        item->setSizeHint(QSize(120, 104));
        item->setData(NodeRole, node->id());
        item->setData(BookmarkDragDrop::FolderRole, node->isFolder());
        item->setData(BookmarkDragDrop::SourceIndexRole, index);
        item->setToolTip(node->name() + QLatin1Char('\n')
            + (node->isFolder() ? QStringLiteral("文件夹 · %1 项").arg(node->children.size()) : node->url())
            + QLatin1Char('\n') + healthResultTooltip(healthResults_.value(node)));
    }
    itemTable_->setRowCount(visibleNodes.size());
    for (int row = 0; row < visibleNodes.size(); ++row) {
        auto* node = visibleNodes[row];
        auto* checkItem = new QTableWidgetItem();
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        checkItem->setCheckState(Qt::Unchecked);
        itemTable_->setItem(row, 0, checkItem);
        auto* nameItem = new QTableWidgetItem(node->name());
        nameItem->setIcon(nodeIcon(node));
        setNodeData(nameItem, node);
        itemTable_->setItem(row, 1, nameItem);
        itemTable_->setItem(row, 2, new QTableWidgetItem(node->displayType()));
        itemTable_->setItem(row, 3, new QTableWidgetItem(node->url()));
        itemTable_->setItem(row, 4, new QTableWidgetItem(node->tagsString()));
        const auto result = healthResults_.value(node);
        const QStringList healthValues = {result.status, codeText(result.code),
            result.elapsedMs > 0 ? QStringLiteral("%1 ms").arg(result.elapsedMs) : QString()};
        for (int column = 0; column < healthValues.size(); ++column) {
            auto* item = new QTableWidgetItem(healthValues[column]);
            item->setToolTip(healthResultTooltip(result));
            itemTable_->setItem(row, column + 5, item);
        }
        itemTable_->setItem(row, 8, new QTableWidgetItem(node->formattedDateAdded()));
    }
    restoreListSelection(selected);
    iconView_->verticalScrollBar()->setValue(iconScroll);
    itemTable_->verticalScrollBar()->setValue(tableScroll);
    setStatus(query.isEmpty()
        ? QStringLiteral("%1 · %2 项   拖入文件夹以移动，拖到图标边缘以排序").arg(folder->path()).arg(visibleNodes.size())
        : QStringLiteral("找到 %1 项 · 搜索时仅支持拖入明确的文件夹，清除搜索后可排序").arg(visibleNodes.size()));
}

void MainWindow::newFolder()
{
    auto* folder = currentFolder();
    if (folder == nullptr) {
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("新建文件夹"), QStringLiteral("文件夹名称："), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }
    document_.addFolder(folder, name.trimmed());
    refreshTree(folder->id());
}

void MainWindow::newBookmark()
{
    auto* folder = currentFolder();
    if (folder == nullptr) {
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("新建书签"), QStringLiteral("书签名称："), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }
    const QString url = QInputDialog::getText(this, QStringLiteral("新建书签"), QStringLiteral("网址："), QLineEdit::Normal, QString(), &ok);
    if (!ok || url.trimmed().isEmpty()) {
        return;
    }
    document_.addBookmark(folder, name.trimmed(), url.trimmed());
    refreshTree(folder->id());
}

void MainWindow::renameSelected()
{
    auto nodes = selectedOperationNodes();
    if (nodes.size() > 1) {
        QMessageBox::information(this, QStringLiteral("重命名"), QStringLiteral("重命名只支持单个项目"));
        return;
    }
    BookmarkNode* node = nodes.size() == 1 ? nodes[0] : currentFolder();
    if (node == nullptr) {
        return;
    }
    const QString fallbackPath = currentFolderId();
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("重命名"), QStringLiteral("新名称："), QLineEdit::Normal, node->name(), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }
    QString error;
    if (!document_.rename(node, name.trimmed(), &error)) {
        QMessageBox::warning(this, QStringLiteral("重命名失败"), error);
        return;
    }
    refreshTree(node->isFolder() ? node->id() : fallbackPath);
}

void MainWindow::editSelectedUrl()
{
    const auto nodes = selectedListNodes();
    if (nodes.size() != 1 || !nodes[0]->isUrl()) {
        QMessageBox::information(this, QStringLiteral("编辑网址"), QStringLiteral("请在右侧列表选择一个书签"));
        return;
    }

    auto* node = nodes[0];
    bool ok = false;
    const QString url = QInputDialog::getText(this, QStringLiteral("编辑网址"), QStringLiteral("网址："), QLineEdit::Normal, node->url(), &ok);
    if (!ok || url.trimmed().isEmpty()) {
        return;
    }

    QString error;
    if (!document_.updateUrl(node, url.trimmed(), &error)) {
        QMessageBox::warning(this, QStringLiteral("编辑网址失败"), error);
        return;
    }
    refreshTree(currentFolderId());
}

void MainWindow::batchEditUrls()
{
    auto nodes = selectedListNodes();
    if (nodes.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("批量编辑"), QStringLiteral("请先勾选或选中要编辑的书签"));
        return;
    }

    // 只保留书签（URL 节点）
    QVector<BookmarkNode*> urlNodes;
    for (auto* node : nodes) {
        if (node->isUrl()) {
            urlNodes.push_back(node);
        }
    }

    if (urlNodes.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("批量编辑"), QStringLiteral("选中的项目中没有书签"));
        return;
    }

    BatchEditDialog dialog(urlNodes.size(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString searchPattern = dialog.searchPattern();
    const QString replacePattern = dialog.replacePattern();

    if (searchPattern.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("批量编辑"), QStringLiteral("查找内容不能为空"));
        return;
    }

    int modified = 0;
    QString error;

    if (dialog.useRegex()) {
        // 正则表达式替换
        QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
        if (!dialog.caseSensitive()) {
            options |= QRegularExpression::CaseInsensitiveOption;
        }

        QRegularExpression regex(searchPattern, options);
        if (!regex.isValid()) {
            QMessageBox::warning(this, QStringLiteral("批量编辑"), QStringLiteral("正则表达式无效: %1").arg(regex.errorString()));
            return;
        }

        for (auto* node : urlNodes) {
            const QString oldUrl = node->url();
            QString newUrl = oldUrl;
            newUrl.replace(regex, replacePattern);
            if (newUrl != oldUrl) {
                if (document_.updateUrl(node, newUrl, &error)) {
                    ++modified;
                } else {
                    QMessageBox::warning(this, QStringLiteral("批量编辑失败"), error);
                    break;
                }
            }
        }
    } else {
        // 普通文本替换
        Qt::CaseSensitivity cs = dialog.caseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive;

        for (auto* node : urlNodes) {
            const QString oldUrl = node->url();
            QString newUrl = oldUrl;
            newUrl.replace(searchPattern, replacePattern, cs);
            if (newUrl != oldUrl) {
                if (document_.updateUrl(node, newUrl, &error)) {
                    ++modified;
                } else {
                    QMessageBox::warning(this, QStringLiteral("批量编辑失败"), error);
                    break;
                }
            }
        }
    }

    refreshTree(currentFolderId());
    setStatus(QStringLiteral("批量编辑完成：已修改 %1 个书签").arg(modified));

    if (modified > 0) {
        QMessageBox::information(this, QStringLiteral("批量编辑"), QStringLiteral("已成功修改 %1 个书签的网址").arg(modified));
    } else {
        QMessageBox::information(this, QStringLiteral("批量编辑"), QStringLiteral("没有找到匹配的网址"));
    }
}

void MainWindow::editTags()
{
    auto nodes = selectedListNodes();
    if (nodes.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("编辑标签"), QStringLiteral("请先选择要编辑标签的书签"));
        return;
    }

    // 只保留书签（URL 节点）
    QVector<BookmarkNode*> urlNodes;
    for (auto* node : nodes) {
        if (node->isUrl()) {
            urlNodes.push_back(node);
        }
    }

    if (urlNodes.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("编辑标签"), QStringLiteral("选中的项目中没有书签"));
        return;
    }

    // 如果选中多个书签，使用第一个的标签作为初始值
    QStringList currentTags = urlNodes[0]->tags;

    TagDialog dialog(currentTags, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QStringList newTags = dialog.tags();

    // 为所有选中的书签设置标签
    for (auto* node : urlNodes) {
        node->tags = newTags;
    }

    document_.setDirty(true);
    refreshList();
    updateDirtyState();
    LOG_INFO(QStringLiteral("Updated tags for %1 bookmarks").arg(urlNodes.size()));
    setStatus(QStringLiteral("已更新 %1 个书签的标签").arg(urlNodes.size()));
}

void MainWindow::deleteSelected()
{
    auto nodes = selectedOperationNodes();
    if (nodes.isEmpty()) {
        auto* folder = currentFolder();
        if (folder != nullptr) {
            nodes.push_back(folder);
        }
    }
    nodes = filterNestedNodes(nodes);
    if (nodes.isEmpty()) {
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("删除"), QStringLiteral("确认删除 %1 个项目？").arg(nodes.size())) != QMessageBox::Yes) {
        return;
    }
    const QString previousPath = currentFolderId();
    QString error;
    for (auto* node : nodes) {
        if (!document_.remove(node, &error)) {
            QMessageBox::warning(this, QStringLiteral("删除失败"), error);
            break;
        }
    }
    refreshTree(previousPath);
}

void MainWindow::moveSelected()
{
    const auto nodes = filterNestedNodes(selectedOperationNodes());
    if (nodes.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("移动"), QStringLiteral("请在右侧列表选择项目，或在左侧勾选文件夹"));
        return;
    }
    auto* target = chooseFolder();
    if (target == nullptr) {
        return;
    }
    const QString previousPath = currentFolderId();
    QString error;
    if (!document_.moveNodes(nodes, target, -1, &error)) {
        QMessageBox::warning(this, QStringLiteral("移动失败"), error);
        return;
    }
    refreshTree(previousPath);
}

void MainWindow::openSelectedUrl()
{
    const auto nodes = selectedListNodes();
    if (nodes.size() != 1) {
        return;
    }
    if (nodes[0]->isFolder()) {
        if (auto* item = findFolderItemById(nodes[0]->id())) {
            folderTree_->setCurrentItem(item);
        }
        return;
    }
    if (nodes[0]->isUrl()) {
        QDesktopServices::openUrl(QUrl(nodes[0]->url()));
    }
}

void MainWindow::checkUrls()
{
    if (health_.isRunning()) {
        return;
    }
    auto* folder = currentFolder();
    if (folder == nullptr) {
        return;
    }
    const auto nodes = collectUrlNodes(folder, includeSubfolders_->isChecked());
    if (nodes.isEmpty()) {
        setStatus(QStringLiteral("当前范围没有书签网址"));
        return;
    }
    for (auto* node : nodes) {
        healthResults_.remove(node);
    }
    refreshList();
    healthTotal_ = nodes.size();
    healthCompleted_ = 0;
    const int maxConcurrent = concurrencySpin_ == nullptr ? health_.maxConcurrent() : concurrencySpin_->value();
    const int timeoutSeconds = timeoutSpin_ == nullptr ? health_.requestTimeoutMs() / 1000 : timeoutSpin_->value();
    health_.setMaxConcurrent(maxConcurrent);
    health_.setRequestTimeoutMs(timeoutSeconds * 1000);
    showProgress(QStringLiteral("网址测活"), QStringLiteral("准备检测网址..."), healthTotal_);
    updateProgress(QStringLiteral("已检测 0 / %1，并发 %2，超时 %3 秒").arg(healthTotal_).arg(maxConcurrent).arg(timeoutSeconds), 0);
    checkButton_->setEnabled(false);
    if (concurrencySpin_ != nullptr) {
        concurrencySpin_->setEnabled(false);
    }
    if (timeoutSpin_ != nullptr) {
        timeoutSpin_->setEnabled(false);
    }
    setStatus(QStringLiteral("开始测活：%1 个网址，并发 %2，超时 %3 秒").arg(nodes.size()).arg(maxConcurrent).arg(timeoutSeconds));
    health_.check(nodes);
}

void MainWindow::scanDuplicates()
{
    QHash<QString, QVector<BookmarkNode*>> groups;
    for (auto* node : document_.allNodes()) {
        if (node == nullptr || !node->isUrl()) {
            continue;
        }
        const QString key = normalizedUrlKey(node->url());
        if (!key.isEmpty()) {
            groups[key].push_back(node);
        }
    }

    QVector<QString> duplicateKeys;
    int duplicateItems = 0;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        if (it.value().size() > 1) {
            duplicateKeys.push_back(it.key());
            duplicateItems += it.value().size();
        }
    }

    if (duplicateKeys.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("重复书签"), QStringLiteral("没有发现重复网址。"));
        setStatus(QStringLiteral("未发现重复书签"));
        return;
    }

    std::sort(duplicateKeys.begin(), duplicateKeys.end());

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("重复书签"));
    dialog.resize(980, 560);

    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        QStringLiteral("发现 %1 组重复网址，共 %2 个书签。以下列表仅用于查看，请回到主窗口进行重命名、移动或删除。")
            .arg(duplicateKeys.size())
            .arg(duplicateItems),
        &dialog));

    auto* table = new QTableWidget(&dialog);
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({QStringLiteral("重复网址"), QStringLiteral("名称"), QStringLiteral("所在文件夹"), QStringLiteral("添加时间")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(table, 1);

    int row = 0;
    for (const auto& key : duplicateKeys) {
        const auto nodes = groups.value(key);
        for (auto* node : nodes) {
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(node->url()));
            table->setItem(row, 1, new QTableWidgetItem(node->name()));
            table->setItem(row, 2, new QTableWidgetItem(node->parent == nullptr ? QString() : node->parent->path()));
            table->setItem(row, 3, new QTableWidgetItem(node->formattedDateAdded()));
            ++row;
        }
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    setStatus(QStringLiteral("发现 %1 组重复网址，共 %2 个书签").arg(duplicateKeys.size()).arg(duplicateItems));
    dialog.exec();
}

void MainWindow::deleteFailedUrls()
{
    const auto nodes = failedHealthNodes();
    if (nodes.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("删除异常链接"), QStringLiteral("没有可删除的异常链接。请先运行网址测活。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("确认删除异常链接"));
    dialog.resize(1080, 560);

    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        QStringLiteral("以下是当前测活结果中的异常链接。只有明确返回 404/410 或网址无效的项目默认勾选；超时、证书和服务器临时异常不会默认勾选。"),
        &dialog));

    auto* table = new QTableWidget(&dialog);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({QStringLiteral("删除"), QStringLiteral("名称"), QStringLiteral("网址"), QStringLiteral("测活状态"), QStringLiteral("状态码"), QStringLiteral("所在文件夹")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    layout->addWidget(table, 1);

    int row = 0;
    for (auto* node : nodes) {
        const auto result = healthResults_.value(node);
        table->insertRow(row);

        auto* checkItem = new QTableWidgetItem();
        checkItem->setFlags((checkItem->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        checkItem->setCheckState(result.definitivelyBroken() ? Qt::Checked : Qt::Unchecked);
        setNodeData(checkItem, node);
        table->setItem(row, 0, checkItem);

        table->setItem(row, 1, new QTableWidgetItem(node->name()));
        table->setItem(row, 2, new QTableWidgetItem(node->url()));
        table->setItem(row, 3, new QTableWidgetItem(result.status));
        table->setItem(row, 4, new QTableWidgetItem(codeText(result.code)));
        table->setItem(row, 5, new QTableWidgetItem(node->parent == nullptr ? QString() : node->parent->path()));
        ++row;
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确认删除"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QVector<BookmarkNode*> selectedNodes;
    for (int i = 0; i < table->rowCount(); ++i) {
        auto* item = table->item(i, 0);
        if (item != nullptr && item->checkState() == Qt::Checked) {
            if (auto* node = nodeFromTableItem(item)) {
                selectedNodes.push_back(node);
            }
        }
    }

    if (selectedNodes.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("删除异常链接"), QStringLiteral("没有勾选任何链接，未执行删除。"));
        return;
    }

    const QString previousPath = currentFolderId();
    int removed = 0;
    QString error;
    for (auto* node : selectedNodes) {
        if (document_.remove(node, &error)) {
            healthResults_.remove(node);
            ++removed;
        } else {
            QMessageBox::warning(this, QStringLiteral("删除异常链接失败"), error);
            break;
        }
    }

    refreshTree(previousPath);
    setStatus(QStringLiteral("已删除 %1 个异常链接").arg(removed));
}

void MainWindow::moveFailedUrls()
{
    const auto nodes = failedHealthNodes();
    if (nodes.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("移动异常链接"), QStringLiteral("没有可移动的异常链接。请先运行网址测活。"));
        return;
    }

    auto* target = chooseFolder();
    if (target == nullptr) {
        return;
    }

    const QString previousPath = currentFolderId();
    QString error;
    if (!document_.moveNodes(nodes, target, -1, &error)) {
        QMessageBox::warning(this, QStringLiteral("移动异常链接失败"), error);
        return;
    }
    const int moved = nodes.size();

    refreshTree(previousPath);
    setStatus(QStringLiteral("已移动 %1 个异常链接到：%2").arg(moved).arg(target->path()));
}

void MainWindow::checkForUpdates()
{
    setStatus(QStringLiteral("正在检查更新..."));
    updater_.checkForUpdates();
}

void MainWindow::onUpdateAvailable(const QString& version, const QString& url, const QString& notes)
{
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(QStringLiteral("发现新版本"));
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText(QStringLiteral("发现新版本 v%1，当前版本 v%2")
        .arg(version)
        .arg(updater_.currentVersion().toString()));
    msgBox.setInformativeText(QStringLiteral("是否打开下载页面？"));

    // 显示更新说明（限制长度）
    QString displayNotes = notes.left(500);
    if (notes.length() > 500) {
        displayNotes += QStringLiteral("\n...");
    }
    msgBox.setDetailedText(displayNotes);

    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);

    if (msgBox.exec() == QMessageBox::Yes) {
        QDesktopServices::openUrl(QUrl(url));
    }

    setStatus(QStringLiteral("发现新版本: v%1").arg(version));
}

void MainWindow::onHealthResult(BookmarkNode* node, const HealthResult& result)
{
    healthResults_.insert(node, result);
    ++healthCompleted_;
    const QString currentName = node == nullptr ? result.url : node->name();
    updateProgress(
        QStringLiteral("正在检测网址... %1 / %2\n%3")
            .arg(healthCompleted_)
            .arg(healthTotal_)
            .arg(currentName),
        healthCompleted_);
    refreshList();
}

void MainWindow::onHealthFinished(int total, int failed)
{
    checkButton_->setEnabled(true);
    if (concurrencySpin_ != nullptr) {
        concurrencySpin_->setEnabled(true);
    }
    if (timeoutSpin_ != nullptr) {
        timeoutSpin_->setEnabled(true);
    }
    updateProgress(QStringLiteral("测活完成：已检测 %1 个，异常 %2 个").arg(total).arg(failed), total);
    closeProgress();
    setStatus(QStringLiteral("测活完成：已检测 %1 个，异常 %2 个").arg(total).arg(failed));
}

void MainWindow::showTreeContextMenu(const QPoint& position)
{
    auto* item = folderTree_->itemAt(position);
    if (item != nullptr) {
        folderTree_->setCurrentItem(item);
    }
    auto* folder = currentFolder();
    if (folder == nullptr) {
        return;
    }

    treeOperation_ = true;
    QMenu menu(this);
    menu.addAction(QStringLiteral("新建文件夹"), this, &MainWindow::newFolder);
    menu.addAction(QStringLiteral("新建书签"), this, &MainWindow::newBookmark);
    menu.addSeparator();

    auto* renameAction = menu.addAction(QStringLiteral("重命名"), this, &MainWindow::renameSelected);
    auto* editUrlAction = menu.addAction(QStringLiteral("编辑网址"), this, &MainWindow::editSelectedUrl);
    auto* deleteAction = menu.addAction(QStringLiteral("删除"), this, &MainWindow::deleteSelected);
    auto* moveAction = menu.addAction(QStringLiteral("移动到"), this, &MainWindow::moveSelected);
    renameAction->setEnabled(!folder->isRoot() || selectedOperationNodes().size() == 1);
    editUrlAction->setEnabled(false);
    deleteAction->setEnabled(!folder->isRoot() || !selectedOperationNodes().isEmpty());
    moveAction->setEnabled(!folder->isRoot() || !selectedOperationNodes().isEmpty());
    menu.addSeparator();
    menu.addAction(QStringLiteral("查找重复书签"), this, &MainWindow::scanDuplicates);
    menu.addAction(QStringLiteral("删除异常链接"), this, &MainWindow::deleteFailedUrls);
    menu.addAction(QStringLiteral("移动异常链接"), this, &MainWindow::moveFailedUrls);

    if (item != nullptr && (item->flags() & Qt::ItemIsUserCheckable)) {
        menu.addSeparator();
        const bool checked = item->checkState(0) == Qt::Checked;
        auto* checkAction = menu.addAction(checked ? QStringLiteral("取消勾选") : QStringLiteral("勾选"));
        connect(checkAction, &QAction::triggered, this, [item, checked]() {
            item->setCheckState(0, checked ? Qt::Unchecked : Qt::Checked);
        });
    }

    menu.exec(folderTree_->viewport()->mapToGlobal(position));
    treeOperation_ = false;
}

void MainWindow::showTableContextMenu(const QPoint& position)
{
    QWidget* viewport = views_->currentIndex() == 0 ? iconView_->viewport() : itemTable_->viewport();
    if (views_->currentIndex() == 0) {
        auto* item = iconView_->itemAt(position);
        if (item && !item->isSelected()) {
            iconView_->clearSelection();
            iconView_->setCurrentItem(item);
            item->setSelected(true);
        } else if (!item) iconView_->clearSelection();
    } else {
        auto* item = itemTable_->itemAt(position);
        if (item && !itemTable_->selectionModel()->isRowSelected(item->row())) {
            itemTable_->clearSelection();
            itemTable_->selectRow(item->row());
        } else if (!item) itemTable_->clearSelection();
    }
    QMenu menu(this);
    const auto nodes = selectedListNodes();
    auto* openAction = menu.addAction(QStringLiteral("打开"), this, &MainWindow::openSelectedUrl);
    openAction->setEnabled(nodes.size() == 1);
    menu.addSeparator();
    menu.addAction(QStringLiteral("全选"), this, [this]() {
        if (views_->currentIndex() == 0) iconView_->selectAll();
        else itemTable_->selectAll();
    });
    menu.addAction(QStringLiteral("取消选择"), this, [this]() { restoreListSelection({}); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("新建文件夹"), this, &MainWindow::newFolder);
    menu.addAction(QStringLiteral("新建书签"), this, &MainWindow::newBookmark);
    menu.addSeparator();
    auto* rename = menu.addAction(QStringLiteral("重命名"), this, &MainWindow::renameSelected);
    auto* edit = menu.addAction(QStringLiteral("编辑网址"), this, &MainWindow::editSelectedUrl);
    auto* batch = menu.addAction(QStringLiteral("批量编辑网址"), this, &MainWindow::batchEditUrls);
    auto* tags = menu.addAction(QStringLiteral("编辑标签"), this, &MainWindow::editTags);
    auto* remove = menu.addAction(QStringLiteral("删除"), this, &MainWindow::deleteSelected);
    auto* move = menu.addAction(QStringLiteral("移动到"), this, &MainWindow::moveSelected);
    rename->setEnabled(nodes.size() == 1);
    edit->setEnabled(nodes.size() == 1 && nodes[0]->isUrl());
    batch->setEnabled(!nodes.isEmpty());
    tags->setEnabled(!nodes.isEmpty());
    remove->setEnabled(!nodes.isEmpty());
    move->setEnabled(!nodes.isEmpty());
    menu.addSeparator();
    menu.addAction(QStringLiteral("网址测活"), this, &MainWindow::checkUrls);
    menu.addAction(QStringLiteral("查找重复书签"), this, &MainWindow::scanDuplicates);
    menu.exec(viewport->mapToGlobal(position));
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Chrome Bookmark Explorer"));
    setWindowIcon(QIcon(":/icons/app.png"));
    resize(1200, 760);
    auto* files = addToolBar(QStringLiteral("文件"));
    files->setMovable(false);
    files->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    profileCombo_ = new QComboBox(this);
    profileCombo_->setMinimumWidth(160);
    profileCombo_->setMaximumWidth(260);
    profileCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    files->addWidget(profileCombo_);
    connect(profileCombo_, qOverload<int>(&QComboBox::activated), this, [this](int) { loadSelectedProfile(); });
    files->addAction(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("刷新 Profile"), this, &MainWindow::reloadProfiles);
    auto* open = files->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), QStringLiteral("打开"), this, &MainWindow::openBookmarksFile);
    open->setShortcut(QKeySequence::Open);
    auto* save = files->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("保存"), this, &MainWindow::saveBookmarks);
    save->setShortcut(QKeySequence::Save);
    files->addAction(QStringLiteral("另存为"), this, &MainWindow::saveBookmarksAs);
    files->addSeparator();
    files->addAction(QStringLiteral("导入"), this, &MainWindow::importBookmarks);
    files->addAction(QStringLiteral("导出"), this, &MainWindow::exportBookmarks);
    files->addAction(QStringLiteral("检查更新"), this, &MainWindow::checkForUpdates);

    addToolBarBreak();
    auto* organize = addToolBar(QStringLiteral("整理"));
    organize->setMovable(false);
    organize->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    organize->addAction(style()->standardIcon(QStyle::SP_FileDialogNewFolder), QStringLiteral("新建文件夹"), this, &MainWindow::newFolder);
    organize->addAction(style()->standardIcon(QStyle::SP_FileIcon), QStringLiteral("新建书签"), this, &MainWindow::newBookmark);
    auto* rename = organize->addAction(QStringLiteral("重命名"), this, &MainWindow::renameSelected);
    rename->setShortcut(Qt::Key_F2);
    organize->addAction(QStringLiteral("编辑网址"), this, &MainWindow::editSelectedUrl);
    organize->addAction(QStringLiteral("批量编辑"), this, &MainWindow::batchEditUrls);
    organize->addAction(QStringLiteral("标签"), this, &MainWindow::editTags);
    organize->addAction(QStringLiteral("移动到"), this, &MainWindow::moveSelected);
    auto* remove = organize->addAction(style()->standardIcon(QStyle::SP_TrashIcon), QStringLiteral("删除"), this, &MainWindow::deleteSelected);
    remove->setShortcut(QKeySequence::Delete);
    organize->addAction(QStringLiteral("查重"), this, &MainWindow::scanDuplicates);

    auto* healthBar = new QToolBar(QStringLiteral("网址测活"), this);
    addToolBar(Qt::BottomToolBarArea, healthBar);
    healthBar->setMovable(false);
    includeSubfolders_ = new QCheckBox(QStringLiteral("含子文件夹"), this);
    includeSubfolders_->setChecked(true);
    healthBar->addWidget(includeSubfolders_);
    healthBar->addWidget(new QLabel(QStringLiteral("  并发 ")));
    concurrencySpin_ = new QSpinBox(this);
    concurrencySpin_->setRange(1, 128);
    concurrencySpin_->setValue(health_.maxConcurrent());
    healthBar->addWidget(concurrencySpin_);
    healthBar->addWidget(new QLabel(QStringLiteral("  超时(秒) ")));
    timeoutSpin_ = new QSpinBox(this);
    timeoutSpin_->setRange(5, 60);
    timeoutSpin_->setValue(health_.requestTimeoutMs() / 1000);
    healthBar->addWidget(timeoutSpin_);
    checkButton_ = new QPushButton(QStringLiteral("网址测活"), this);
    healthBar->addWidget(checkButton_);
    connect(checkButton_, &QPushButton::clicked, this, &MainWindow::checkUrls);
    healthBar->addAction(QStringLiteral("删除异常"), this, &MainWindow::deleteFailedUrls);
    healthBar->addAction(QStringLiteral("移动异常"), this, &MainWindow::moveFailedUrls);

    auto* splitter = new QSplitter(this);
    folderTree_ = new BookmarkFolderTree(splitter);
    folderTree_->setObjectName(QStringLiteral("folderTree"));
    folderTree_->setHeaderLabel(QStringLiteral("文件夹"));
    folderTree_->setMinimumWidth(160);
    folderTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    folderTree_->setAlternatingRowColors(true);
    connect(folderTree_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::refreshList);
    connect(folderTree_, &QTreeWidget::customContextMenuRequested, this, &MainWindow::showTreeContextMenu);

    auto* browser = new QWidget(splitter);
    auto* layout = new QVBoxLayout(browser);
    layout->setContentsMargins(12, 8, 12, 8);
    auto* navigation = new QHBoxLayout;
    upButton_ = new BookmarkPathButton(browser);
    upButton_->setObjectName(QStringLiteral("upButton"));
    upButton_->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    upButton_->setToolTip(QStringLiteral("返回上一级 · 可拖入书签"));
    upButton_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
    upButton_->setEnabled(false);
    navigation->addWidget(upButton_);
    connect(upButton_, &QToolButton::clicked, this, [this]() { navigateTo(upButton_->folderId); });
    auto* breadcrumbs = new QWidget(browser);
    breadcrumbLayout_ = new QHBoxLayout(breadcrumbs);
    breadcrumbLayout_->setContentsMargins(0, 0, 0, 0);
    breadcrumbLayout_->setSpacing(2);
    auto* pathScroll = new QScrollArea(browser);
    pathScroll->setWidgetResizable(true);
    pathScroll->setWidget(breadcrumbs);
    pathScroll->setFrameShape(QFrame::NoFrame);
    pathScroll->setFixedHeight(40);
    pathScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pathScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navigation->addWidget(pathScroll, 1);
    auto* viewToolbar = new QToolBar(browser);
    auto* viewGroup = new QActionGroup(this);
    for (int index = 0; index < 2; ++index) {
        auto* action = viewToolbar->addAction(index == 0 ? QStringLiteral("图标") : QStringLiteral("详情"));
        action->setObjectName(index == 0 ? QStringLiteral("iconMode") : QStringLiteral("detailMode"));
        action->setCheckable(true);
        action->setChecked(index == 0);
        viewGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, index]() { switchView(index); });
    }
    navigation->addWidget(viewToolbar);
    layout->addLayout(navigation);
    searchEdit_ = new QLineEdit(browser);
    searchEdit_->setObjectName(QStringLiteral("bookmarkSearch"));
    searchEdit_->setPlaceholderText(QStringLiteral("搜索当前文件夹的名称或网址…"));
    searchEdit_->setClearButtonEnabled(true);
    layout->addWidget(searchEdit_);
    connect(searchEdit_, &QLineEdit::textChanged, this, &MainWindow::refreshList);

    views_ = new QStackedWidget(browser);
    iconView_ = new BookmarkIconView(views_);
    iconView_->setObjectName(QStringLiteral("bookmarkIcons"));
    views_->addWidget(iconView_);
    itemTable_ = new QTableWidget(views_);
    itemTable_->setObjectName(QStringLiteral("bookmarkDetails"));
    views_->addWidget(itemTable_);
    layout->addWidget(views_, 1);
    itemTable_->setColumnCount(9);
    itemTable_->setHorizontalHeaderLabels({QStringLiteral(""), QStringLiteral("名称"), QStringLiteral("类型"), QStringLiteral("网址"), QStringLiteral("标签"), QStringLiteral("测活"), QStringLiteral("状态码"), QStringLiteral("耗时"), QStringLiteral("添加时间")});
    itemTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    itemTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    itemTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    itemTable_->setColumnWidth(0, 32);
    itemTable_->setColumnWidth(1, 180);
    itemTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    itemTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    itemTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    itemTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    itemTable_->setAlternatingRowColors(true);
    // Detail mode is for inspection; icon mode owns positional drag/drop.
    itemTable_->setDragDropMode(QAbstractItemView::NoDragDrop);
    connect(itemTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (auto* node = nodeFromTableItem(itemTable_->item(row, 1))) {
            if (node->isFolder()) navigateTo(node->id());
            else QDesktopServices::openUrl(QUrl(node->url()));
        }
    });
    connect(iconView_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (auto* node = document_.nodeById(item->data(NodeRole).toString())) {
            if (node->isFolder()) navigateTo(node->id());
            else QDesktopServices::openUrl(QUrl(node->url()));
        }
    });
    connect(iconView_, &QListWidget::customContextMenuRequested, this, &MainWindow::showTableContextMenu);
    connect(itemTable_, &QTableWidget::customContextMenuRequested, this, &MainWindow::showTableContextMenu);
    const auto makeMime = [this](const QStringList& ids) { return createDrag(ids); };
    const auto drop = [this](const QMimeData* mime, const QString& target, int index, bool commit) {
        return handleDrop(mime, target, index, commit);
    };
    folderTree_->makeMime = makeMime;
    folderTree_->handleDrop = drop;
    iconView_->makeMime = makeMime;
    iconView_->handleDrop = drop;
    upButton_->handleDrop = drop;
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({230, 970});
    setCentralWidget(splitter);
    refreshList();
}

void MainWindow::refreshTree(const QString& preferredId)
{
    QStringList ancestors;
    for (auto* item = folderTree_->currentItem(); item; item = item->parent()) {
        ancestors.append(item->data(0, NodeRole).toString());
    }
    QSet<QString> checked;
    QSet<QString> expanded;
    for (QTreeWidgetItemIterator it(folderTree_); *it; ++it) {
        const QString id = (*it)->data(0, NodeRole).toString();
        if ((*it)->checkState(0) == Qt::Checked) checked.insert(id);
        if ((*it)->isExpanded()) expanded.insert(id);
    }
    {
        QSignalBlocker blocker(folderTree_);
        folderTree_->clear();
        for (const auto& root : document_.roots()) addFolderItem(nullptr, root.get());
        for (QTreeWidgetItemIterator it(folderTree_); *it; ++it) {
            const QString id = (*it)->data(0, NodeRole).toString();
            if (checked.contains(id) && ((*it)->flags() & Qt::ItemIsUserCheckable)) (*it)->setCheckState(0, Qt::Checked);
            (*it)->setExpanded(expanded.contains(id));
        }
        auto* selected = findFolderItemById(preferredId);
        for (const auto& id : ancestors) {
            if (!selected) selected = findFolderItemById(id);
        }
        if (!selected && folderTree_->topLevelItemCount()) selected = folderTree_->topLevelItem(0);
        if (selected) {
            for (auto* item = selected; item; item = item->parent()) item->setExpanded(true);
            folderTree_->setCurrentItem(selected);
        }
    }
    // Removed nodes may still be keys in old health results. Do not dereference them.
    const auto allNodes = document_.allNodes();
    const QSet<BookmarkNode*> live(allNodes.begin(), allNodes.end());
    for (auto it = healthResults_.begin(); it != healthResults_.end();) {
        if (!live.contains(it.key())) it = healthResults_.erase(it);
        else ++it;
    }
    updateNavigation();
    refreshList();
    updateDirtyState();
}

void MainWindow::addFolderItem(QTreeWidgetItem* parentItem, BookmarkNode* node)
{
    auto* item = parentItem == nullptr ? new QTreeWidgetItem(folderTree_) : new QTreeWidgetItem(parentItem);
    item->setText(0, node->name());
    item->setIcon(0, QIcon(":/icons/folder.png"));
    if (!node->isRoot()) {
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Unchecked);
    }
    setNodeData(item, node);
    for (const auto& child : node->children) {
        if (child->isFolder()) {
            addFolderItem(item, child.get());
        }
    }
}

BookmarkNode* MainWindow::currentFolder() const
{
    return nodeFromItem(folderTree_->currentItem());
}

QVector<BookmarkNode*> MainWindow::selectedListNodes() const
{
    QVector<BookmarkNode*> nodes;
    if (views_->currentIndex() == 0) {
        for (const auto& id : iconView_->selectedNodeIds()) {
            if (auto* node = document_.nodeById(id)) nodes.append(node);
        }
        return nodes;
    }
    bool hasChecked = false;
    for (int row = 0; row < itemTable_->rowCount(); ++row) {
        if (itemTable_->item(row, 0)->checkState() == Qt::Checked) hasChecked = true;
    }
    for (int row = 0; row < itemTable_->rowCount(); ++row) {
        if (hasChecked ? itemTable_->item(row, 0)->checkState() == Qt::Checked
                       : itemTable_->selectionModel()->isRowSelected(row)) {
            if (auto* node = nodeFromTableItem(itemTable_->item(row, 1))) nodes.append(node);
        }
    }
    return nodes;
}

QVector<BookmarkNode*> MainWindow::checkedFolderNodes() const
{
    QVector<BookmarkNode*> nodes;
    for (int i = 0; i < folderTree_->topLevelItemCount(); ++i) {
        collectCheckedFolderNodes(folderTree_->topLevelItem(i), &nodes);
    }
    return filterNestedNodes(nodes);
}

QVector<BookmarkNode*> MainWindow::selectedOperationNodes() const
{
    if (treeOperation_ || folderTree_->hasFocus()) {
        auto nodes = checkedFolderNodes();
        if (nodes.isEmpty() && currentFolder()) nodes.append(currentFolder());
        return filterNestedNodes(nodes);
    }
    return filterNestedNodes(selectedListNodes());
}

QVector<BookmarkNode*> MainWindow::collectUrlNodes(BookmarkNode* folder, bool recursive) const
{
    QVector<BookmarkNode*> nodes;
    if (folder == nullptr) {
        return nodes;
    }
    for (const auto& child : folder->children) {
        if (child->isUrl()) {
            nodes.push_back(child.get());
        } else if (recursive && child->isFolder()) {
            nodes += collectUrlNodes(child.get(), true);
        }
    }
    return nodes;
}

QVector<BookmarkNode*> MainWindow::collectFolders() const
{
    QVector<BookmarkNode*> result;
    for (auto* node : document_.folders()) {
        result.push_back(node);
    }
    return result;
}

BookmarkNode* MainWindow::chooseFolder()
{
    const auto folders = collectFolders();
    QStringList labels;
    for (auto* folder : folders) {
        labels << QStringLiteral("%1  [#%2]").arg(folder->path(), folder->id());
    }
    bool ok = false;
    const QString selected = QInputDialog::getItem(this, QStringLiteral("选择目标文件夹"), QStringLiteral("移动到："), labels, 0, false, &ok);
    if (!ok || selected.isEmpty()) {
        return nullptr;
    }
    const int index = labels.indexOf(selected);
    return index >= 0 ? folders[index] : nullptr;
}

QVector<BookmarkNode*> MainWindow::failedHealthNodes() const
{
    QVector<BookmarkNode*> nodes;
    QSet<BookmarkNode*> seen;
    for (auto it = healthResults_.cbegin(); it != healthResults_.cend(); ++it) {
        auto* node = it.key();
        if (node != nullptr && node->isUrl() && !it.value().ok() && !seen.contains(node)) {
            seen.insert(node);
            nodes.push_back(node);
        }
    }
    return nodes;
}

bool MainWindow::confirmDiscardChanges()
{
    if (!document_.isDirty()) {
        return true;
    }

    const auto choice = QMessageBox::warning(
        this,
        QStringLiteral("未保存的更改"),
        QStringLiteral("当前收藏夹有未保存的更改，是否先保存？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (choice == QMessageBox::Save) {
        return saveBookmarksInternal(false);
    }
    return choice == QMessageBox::Discard;
}

void MainWindow::updateDirtyState()
{
    const QString suffix = document_.isDirty() ? QStringLiteral(" *") : QString();
    setWindowTitle(QStringLiteral("Chrome Bookmark Explorer%1").arg(suffix));
}

QString MainWindow::currentFolderId() const
{
    auto* item = folderTree_->currentItem();
    return item ? item->data(0, NodeRole).toString() : QString();
}

void MainWindow::collectCheckedFolderNodes(QTreeWidgetItem* item, QVector<BookmarkNode*>* nodes) const
{
    if (item == nullptr || nodes == nullptr) {
        return;
    }
    if ((item->flags() & Qt::ItemIsUserCheckable) && item->checkState(0) == Qt::Checked) {
        if (auto* node = nodeFromItem(item)) {
            nodes->push_back(node);
        }
    }
    for (int i = 0; i < item->childCount(); ++i) {
        collectCheckedFolderNodes(item->child(i), nodes);
    }
}



QVector<BookmarkNode*> MainWindow::filterNestedNodes(const QVector<BookmarkNode*>& nodes) const
{
    QVector<BookmarkNode*> result;
    QSet<BookmarkNode*> candidates;
    for (auto* node : nodes) {
        if (node != nullptr) {
            candidates.insert(node);
        }
    }
    QSet<BookmarkNode*> added;
    for (auto* node : nodes) {
        if (node != nullptr && !added.contains(node) && !isDescendantOfAny(node, candidates)) {
            added.insert(node);
            result.push_back(node);
        }
    }
    return result;
}

QTreeWidgetItem* MainWindow::findFolderItemById(const QString& id) const
{
    if (id.isEmpty()) return nullptr;
    for (QTreeWidgetItemIterator it(folderTree_); *it; ++it) {
        if ((*it)->data(0, NodeRole).toString() == id) return *it;
    }
    return nullptr;
}

bool MainWindow::isDescendantOfAny(BookmarkNode* node, const QSet<BookmarkNode*>& candidates) const
{
    for (auto* parent = node == nullptr ? nullptr : node->parent; parent != nullptr; parent = parent->parent) {
        if (candidates.contains(parent)) {
            return true;
        }
    }
    return false;
}

void MainWindow::setStatus(const QString& text)
{
    statusBar()->showMessage(text);
}

void MainWindow::showProgress(const QString& title, const QString& label, int maximum)
{
    closeProgress();
    progressDialog_ = new QProgressDialog(label, QString(), 0, maximum, this);
    progressDialog_->setWindowTitle(title);
    progressDialog_->setCancelButton(nullptr);
    progressDialog_->setWindowModality(Qt::ApplicationModal);
    progressDialog_->setMinimumDuration(0);
    progressDialog_->setValue(0);
    progressDialog_->show();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void MainWindow::updateProgress(const QString& label, int value)
{
    if (progressDialog_ == nullptr) {
        return;
    }
    progressDialog_->setLabelText(label);
    progressDialog_->setValue(value);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void MainWindow::closeProgress()
{
    if (progressDialog_ == nullptr) {
        return;
    }
    progressDialog_->close();
    progressDialog_->deleteLater();
    progressDialog_ = nullptr;
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

BookmarkNode* MainWindow::nodeFromItem(QTreeWidgetItem* item) const
{
    if (item == nullptr) {
        return nullptr;
    }
    return document_.nodeById(item->data(0, NodeRole).toString());
}

BookmarkNode* MainWindow::nodeFromTableItem(QTableWidgetItem* item) const
{
    if (item == nullptr) {
        return nullptr;
    }
    return document_.nodeById(item->data(NodeRole).toString());
}

void MainWindow::setNodeData(QTreeWidgetItem* item, BookmarkNode* node)
{
    item->setData(0, NodeRole, node->id());
}

void MainWindow::setNodeData(QTableWidgetItem* item, BookmarkNode* node)
{
    item->setData(NodeRole, node->id());
}

void MainWindow::navigateTo(const QString& id)
{
    if (auto* item = findFolderItemById(id)) {
        searchEdit_->clear();
        folderTree_->setCurrentItem(item);
        folderTree_->scrollToItem(item);
    }
}

void MainWindow::updateNavigation()
{
    while (auto* item = breadcrumbLayout_->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    auto* folder = currentFolder();
    upButton_->folderId = folder && folder->parent ? folder->parent->id() : QString();
    upButton_->setEnabled(!upButton_->folderId.isEmpty());
    QVector<BookmarkNode*> path;
    for (auto* node = folder; node; node = node->parent) path.prepend(node);
    for (auto* node : path) {
        auto* button = new BookmarkPathButton;
        button->folderId = node->id();
        button->setText(fontMetrics().elidedText(node->name(), Qt::ElideMiddle, 180));
        button->setToolTip(node->path() + QStringLiteral("\n点击进入 · 拖入书签以移动"));
        button->handleDrop = iconView_->handleDrop;
        connect(button, &QToolButton::clicked, this, [this, id = node->id()]() { navigateTo(id); });
        breadcrumbLayout_->addWidget(button);
        if (node != folder) breadcrumbLayout_->addWidget(new QLabel(QStringLiteral("›")));
    }
    breadcrumbLayout_->addStretch();
}

QStringList MainWindow::selectedListIds() const
{
    QStringList ids;
    for (auto* node : selectedListNodes()) ids.append(node->id());
    return ids;
}

void MainWindow::restoreListSelection(const QStringList& ids)
{
    const QSet<QString> selected(ids.begin(), ids.end());
    iconView_->clearSelection();
    itemTable_->clearSelection();
    for (int index = 0; index < iconView_->count(); ++index) {
        auto* item = iconView_->item(index);
        item->setSelected(selected.contains(item->data(NodeRole).toString()));
    }
    for (int row = 0; row < itemTable_->rowCount(); ++row) {
        const bool select = selected.contains(itemTable_->item(row, 1)->data(NodeRole).toString());
        itemTable_->item(row, 0)->setCheckState(Qt::Unchecked);
        if (select) itemTable_->selectionModel()->select(itemTable_->model()->index(row, 1),
            QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
}

void MainWindow::switchView(int index)
{
    const auto ids = selectedListIds();
    views_->setCurrentIndex(index);
    restoreListSelection(ids);
    if (index == 0) iconView_->setFocus();
    else itemTable_->setFocus();
}

QIcon MainWindow::nodeIcon(const BookmarkNode* node)
{
    if (node->isFolder()) {
        const QIcon icon(QStringLiteral(":/icons/folder.png"));
        return icon.isNull() ? style()->standardIcon(QStyle::SP_DirIcon) : icon;
    }
    QString host = QUrl(node->url()).host().toLower();
    if (host.startsWith(QStringLiteral("www."))) host.remove(0, 4);
    if (host.isEmpty()) host = QUrl(node->url()).scheme();
    if (host.isEmpty()) host = QStringLiteral("?");
    if (siteIcons_.contains(host)) return siteIcons_.value(host);
    const QByteArray hash = QCryptographicHash::hash(host.toUtf8(), QCryptographicHash::Sha256);
    const QColor color = QColor::fromHsv(static_cast<unsigned char>(hash[0]) * 359 / 255, 145, 155);
    QPixmap pixmap(96, 96);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(2, 2, 44, 44), 11, 11);
    auto font = painter.font();
    font.setPixelSize(25);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(QRect(2, 2, 44, 44), Qt::AlignCenter, host.left(1).toUpper());
    painter.end();
    const QIcon icon(pixmap);
    siteIcons_.insert(host, icon);
    return icon;
}

QMimeData* MainWindow::createDrag(const QStringList& ids) const
{
    if (health_.isRunning() || ids.isEmpty()) return nullptr;
    for (const auto& id : ids) {
        auto* node = document_.nodeById(id);
        if (!node || !node->parent) return nullptr;
    }
    return BookmarkDragDrop::encode({document_.generation(), document_.revision(), ids});
}

bool MainWindow::handleDrop(const QMimeData* mime, const QString& targetId, int index, bool commit)
{
    BookmarkDragDrop::Payload payload;
    if (health_.isRunning() || !BookmarkDragDrop::decode(mime, &payload)
        || payload.generation != document_.generation() || payload.revision != document_.revision()) return false;
    auto* target = document_.nodeById(targetId);
    if (!target || !target->isFolder() || index < -1 || index > static_cast<int>(target->children.size())) return false;
    const bool filteredTarget = !searchEdit_->text().trimmed().isEmpty() && targetId == currentFolderId();
    // A filtered folder only accepts explicit cross-folder appends, never reordering.
    if (filteredTarget && index != -1) return false;
    QVector<BookmarkNode*> nodes;
    for (const auto& id : payload.ids) {
        auto* node = document_.nodeById(id);
        if (!node || !node->parent || (filteredTarget && node->parent == target)) return false;
        for (auto* ancestor = target; ancestor; ancestor = ancestor->parent) {
            if (ancestor == node) return false;
        }
        nodes.append(node);
    }
    if (!commit) return true;
    const auto revision = document_.revision();
    const QString currentId = currentFolderId();
    QString error;
    if (!document_.moveNodes(nodes, target, index, &error)) {
        setStatus(QStringLiteral("移动失败：%1").arg(error));
        return false;
    }
    if (revision == document_.revision()) return true;
    updateDirtyState();
    // A path button may be the receiver of the current drop event; rebuild after it returns.
    QTimer::singleShot(0, this, [this, currentId, ids = payload.ids, generation = payload.generation]() {
        if (generation != document_.generation()) return;
        refreshTree(currentId);
        restoreListSelection(ids);
        setStatus(QStringLiteral("已移动 %1 个项目 · 尚未保存（Ctrl+S 保存）").arg(ids.size()));
    });
    return true;
}
