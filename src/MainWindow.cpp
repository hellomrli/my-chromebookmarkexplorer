#include "MainWindow.h"
#include "BatchEditDialog.h"
#include "ImportExport.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
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

#include <algorithm>

namespace {
constexpr int NodeRole = Qt::UserRole + 1;

QString codeText(int code)
{
    return code > 0 ? QString::number(code) : QString();
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

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    buildUi();
    connect(&health_, &HealthChecker::resultReady, this, &MainWindow::onHealthResult);
    connect(&health_, &HealthChecker::finished, this, &MainWindow::onHealthFinished);
    connect(&updater_, &Updater::updateAvailable, this, &MainWindow::onUpdateAvailable);
    connect(&updater_, &Updater::checkFailed, this, [this](const QString& error) {
        setStatus(QStringLiteral("检查更新失败: %1").arg(error));
    });
    reloadProfiles();

    // 启动时自动检查更新
    QTimer::singleShot(2000, &updater_, &Updater::checkForUpdates);
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
    if (!document_.load(profiles_[index].bookmarksPath, &error)) {
        QSignalBlocker blocker(profileCombo_);
        profileCombo_->setCurrentIndex(loadedProfileIndex_);
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
    if (!document_.load(path, &error)) {
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
    if (document_.roots().isEmpty()) {
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
        setStatus(QStringLiteral("已导出到：%1").arg(filePath));
    } else {
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
        QMessageBox::critical(this, QStringLiteral("导入失败"), error);
        return;
    }

    // 将导入的节点添加到当前文档
    auto* targetFolder = currentFolder();
    if (!targetFolder) {
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
                child.release(); // 转移所有权
                ++imported;
            }
        }
        delete root;
    }

    refreshTree(currentFolderPath());
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
        QMessageBox::critical(this, QStringLiteral("保存失败"), error);
        return false;
    }
    updateProgress(QStringLiteral("保存完成"), 3);
    closeProgress();
    updateDirtyState();
    setStatus(QStringLiteral("已保存并自动备份：%1").arg(target));
    return true;
}

void MainWindow::refreshList()
{
    itemTable_->setRowCount(0);
    auto* folder = currentFolder();
    if (folder == nullptr) {
        return;
    }

    const QString query = searchEdit_->text().trimmed();
    int row = 0;
    for (const auto& child : folder->children) {
        BookmarkNode* node = child.get();
        if (!query.isEmpty()
            && !node->name().contains(query, Qt::CaseInsensitive)
            && !node->url().contains(query, Qt::CaseInsensitive)) {
            continue;
        }

        itemTable_->insertRow(row);

        // 勾选框
        auto* checkItem = new QTableWidgetItem();
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        checkItem->setCheckState(Qt::Unchecked);
        itemTable_->setItem(row, 0, checkItem);

        // 名称（带图标）
        auto* nameItem = new QTableWidgetItem(node->name());
        nameItem->setIcon(node->isFolder() ? QIcon(":/icons/folder.png") : QIcon(":/icons/bookmark.png"));
        setNodeData(nameItem, node);
        itemTable_->setItem(row, 1, nameItem);

        itemTable_->setItem(row, 2, new QTableWidgetItem(node->displayType()));
        itemTable_->setItem(row, 3, new QTableWidgetItem(node->url()));

        const auto result = healthResults_.value(node);
        itemTable_->setItem(row, 4, new QTableWidgetItem(result.status));
        itemTable_->setItem(row, 5, new QTableWidgetItem(codeText(result.code)));
        itemTable_->setItem(row, 6, new QTableWidgetItem(result.elapsedMs > 0 ? QStringLiteral("%1 ms").arg(result.elapsedMs) : QString()));
        itemTable_->setItem(row, 7, new QTableWidgetItem(node->formattedDateAdded()));
        ++row;
    }
    setStatus(QStringLiteral("%1：%2 项").arg(folder->path()).arg(folder->children.size()));
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
    refreshTree(folder->path());
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
    refreshTree(folder->path());
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
    const QString fallbackPath = currentFolderPath();
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
    refreshTree(node->isFolder() ? node->path() : fallbackPath);
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
    refreshTree(currentFolderPath());
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
            const QString newUrl = oldUrl.replace(regex, replacePattern);
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

    refreshTree(currentFolderPath());
    setStatus(QStringLiteral("批量编辑完成：已修改 %1 个书签").arg(modified));

    if (modified > 0) {
        QMessageBox::information(this, QStringLiteral("批量编辑"), QStringLiteral("已成功修改 %1 个书签的网址").arg(modified));
    } else {
        QMessageBox::information(this, QStringLiteral("批量编辑"), QStringLiteral("没有找到匹配的网址"));
    }
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
    const QString previousPath = currentFolderPath();
    QString error;
    for (auto* node : nodes) {
        if (!document_.remove(node, &error)) {
            QMessageBox::warning(this, QStringLiteral("删除失败"), error);
            break;
        }
    }
    refreshTree(nearestExistingFolderPath(previousPath));
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
    const QString previousPath = currentFolderPath();
    QString error;
    for (auto* node : nodes) {
        if (!document_.move(node, target, &error)) {
            QMessageBox::warning(this, QStringLiteral("移动失败"), error);
            break;
        }
    }
    refreshTree(nearestExistingFolderPath(previousPath));
}

void MainWindow::openSelectedUrl()
{
    const auto nodes = selectedListNodes();
    if (nodes.size() != 1) {
        return;
    }
    if (nodes[0]->isFolder()) {
        if (auto* item = findFolderItemByPath(nodes[0]->path())) {
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
    healthTotal_ = nodes.size();
    healthCompleted_ = 0;
    const int maxConcurrent = concurrencySpin_ == nullptr ? health_.maxConcurrent() : concurrencySpin_->value();
    health_.setMaxConcurrent(maxConcurrent);
    showProgress(QStringLiteral("网址测活"), QStringLiteral("准备检测网址..."), healthTotal_);
    updateProgress(QStringLiteral("已检测 0 / %1，并发 %2").arg(healthTotal_).arg(maxConcurrent), 0);
    checkButton_->setEnabled(false);
    if (concurrencySpin_ != nullptr) {
        concurrencySpin_->setEnabled(false);
    }
    setStatus(QStringLiteral("开始测活：%1 个网址，并发 %2").arg(nodes.size()).arg(maxConcurrent));
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
        QStringLiteral("以下是当前测活结果中的异常链接，默认全选。请取消勾选不想删除的链接，然后点击“确认删除”。"),
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
        checkItem->setCheckState(Qt::Checked);
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

    const QString previousPath = currentFolderPath();
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

    refreshTree(nearestExistingFolderPath(previousPath));
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

    const QString previousPath = currentFolderPath();
    int moved = 0;
    QString error;
    for (auto* node : nodes) {
        if (document_.move(node, target, &error)) {
            ++moved;
        } else {
            QMessageBox::warning(this, QStringLiteral("移动异常链接失败"), error);
            break;
        }
    }

    refreshTree(nearestExistingFolderPath(previousPath));
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
}

void MainWindow::showTableContextMenu(const QPoint& position)
{
    auto* item = itemTable_->itemAt(position);
    if (item != nullptr) {
        bool rowAlreadySelected = false;
        const int row = item->row();
        const auto ranges = itemTable_->selectedRanges();
        for (const auto& range : ranges) {
            if (row >= range.topRow() && row <= range.bottomRow()) {
                rowAlreadySelected = true;
                break;
            }
        }
        if (!rowAlreadySelected) {
            itemTable_->clearSelection();
            itemTable_->selectRow(row);
        }
    }

    QMenu menu(this);
    const auto nodes = selectedListNodes();
    auto* openAction = menu.addAction(QStringLiteral("打开网址"), this, &MainWindow::openSelectedUrl);
    openAction->setEnabled(nodes.size() == 1 && nodes[0]->isUrl());
    menu.addSeparator();

    // 全选/取消全选勾选框
    auto* selectAllAction = menu.addAction(QStringLiteral("全选"));
    connect(selectAllAction, &QAction::triggered, this, [this]() {
        for (int row = 0; row < itemTable_->rowCount(); ++row) {
            if (auto* checkItem = itemTable_->item(row, 0)) {
                checkItem->setCheckState(Qt::Checked);
            }
        }
    });

    auto* unselectAllAction = menu.addAction(QStringLiteral("取消全选"));
    connect(unselectAllAction, &QAction::triggered, this, [this]() {
        for (int row = 0; row < itemTable_->rowCount(); ++row) {
            if (auto* checkItem = itemTable_->item(row, 0)) {
                checkItem->setCheckState(Qt::Unchecked);
            }
        }
    });

    menu.addSeparator();
    menu.addAction(QStringLiteral("新建文件夹"), this, &MainWindow::newFolder);
    menu.addAction(QStringLiteral("新建书签"), this, &MainWindow::newBookmark);
    menu.addSeparator();
    auto* renameAction = menu.addAction(QStringLiteral("重命名"), this, &MainWindow::renameSelected);
    auto* editUrlAction = menu.addAction(QStringLiteral("编辑网址"), this, &MainWindow::editSelectedUrl);
    auto* batchEditAction = menu.addAction(QStringLiteral("批量编辑网址"), this, &MainWindow::batchEditUrls);
    auto* deleteAction = menu.addAction(QStringLiteral("删除"), this, &MainWindow::deleteSelected);
    auto* moveAction = menu.addAction(QStringLiteral("移动到"), this, &MainWindow::moveSelected);
    const bool hasOperationNodes = !selectedOperationNodes().isEmpty();
    renameAction->setEnabled(selectedOperationNodes().size() <= 1);
    editUrlAction->setEnabled(nodes.size() == 1 && nodes[0]->isUrl());
    batchEditAction->setEnabled(!nodes.isEmpty());
    deleteAction->setEnabled(hasOperationNodes || currentFolder() != nullptr);
    moveAction->setEnabled(hasOperationNodes);
    menu.addSeparator();
    menu.addAction(QStringLiteral("网址测活"), this, &MainWindow::checkUrls);
    menu.addAction(QStringLiteral("查找重复书签"), this, &MainWindow::scanDuplicates);
    menu.addAction(QStringLiteral("删除异常链接"), this, &MainWindow::deleteFailedUrls);
    menu.addAction(QStringLiteral("移动异常链接"), this, &MainWindow::moveFailedUrls);

    menu.exec(itemTable_->viewport()->mapToGlobal(position));
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Chrome Bookmark Explorer"));
    setWindowIcon(QIcon(":/icons/app.png"));
    resize(1200, 760);

    auto* toolbar = addToolBar(QStringLiteral("工具栏"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(20, 20));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    toolbar->addWidget(new QLabel(QStringLiteral(" Profile ")));
    profileCombo_ = new QComboBox(this);
    profileCombo_->setMinimumWidth(280);
    toolbar->addWidget(profileCombo_);
    connect(profileCombo_, qOverload<int>(&QComboBox::activated), this, [this](int) { loadSelectedProfile(); });

    auto* refreshAction = toolbar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("刷新"), this, &MainWindow::reloadProfiles);
    auto* openAction = toolbar->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), QStringLiteral("打开文件"), this, &MainWindow::openBookmarksFile);
    auto* saveAction = toolbar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("保存"), this, &MainWindow::saveBookmarks);
    toolbar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("另存为"), this, &MainWindow::saveBookmarksAs);
    toolbar->addSeparator();
    toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), QStringLiteral("导出"), this, &MainWindow::exportBookmarks);
    toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), QStringLiteral("导入"), this, &MainWindow::importBookmarks);

    toolbar->addSeparator();
    auto* newFolderAction = toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogNewFolder), QStringLiteral("新建文件夹"), this, &MainWindow::newFolder);
    auto* newBookmarkAction = toolbar->addAction(style()->standardIcon(QStyle::SP_FileIcon), QStringLiteral("新建书签"), this, &MainWindow::newBookmark);
    toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), QStringLiteral("重命名"), this, &MainWindow::renameSelected);
    toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), QStringLiteral("编辑网址"), this, &MainWindow::editSelectedUrl);
    toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), QStringLiteral("批量编辑"), this, &MainWindow::batchEditUrls);
    toolbar->addAction(style()->standardIcon(QStyle::SP_TrashIcon), QStringLiteral("删除"), this, &MainWindow::deleteSelected);
    toolbar->addAction(style()->standardIcon(QStyle::SP_ArrowRight), QStringLiteral("移动到"), this, &MainWindow::moveSelected);
    toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), QStringLiteral("查重"), this, &MainWindow::scanDuplicates);
    toolbar->addSeparator();
    toolbar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("检查更新"), this, &MainWindow::checkForUpdates);

    toolbar->addSeparator();

    includeSubfolders_ = new QCheckBox(QStringLiteral("含子文件夹"), this);
    includeSubfolders_->setChecked(true);
    toolbar->addWidget(includeSubfolders_);

    toolbar->addWidget(new QLabel(QStringLiteral(" 并发数 ")));
    concurrencySpin_ = new QSpinBox(this);
    concurrencySpin_->setRange(1, 128);
    concurrencySpin_->setValue(health_.maxConcurrent());
    concurrencySpin_->setToolTip(QStringLiteral("同时测活的网址数量；数值越大速度越快，但也更容易触发站点限速"));
    concurrencySpin_->setMaximumWidth(72);
    toolbar->addWidget(concurrencySpin_);

    checkButton_ = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("网址测活"), this);
    toolbar->addWidget(checkButton_);
    connect(checkButton_, &QPushButton::clicked, this, &MainWindow::checkUrls);
    toolbar->addAction(QStringLiteral("删除异常"), this, &MainWindow::deleteFailedUrls);
    toolbar->addAction(QStringLiteral("移动异常"), this, &MainWindow::moveFailedUrls);

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral(" 搜索 ")));
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText(QStringLiteral("搜索书签名称或网址..."));
    searchEdit_->setMaximumWidth(280);
    toolbar->addWidget(searchEdit_);
    connect(searchEdit_, &QLineEdit::textChanged, this, &MainWindow::refreshList);

    auto* splitter = new QSplitter(this);
    folderTree_ = new QTreeWidget(splitter);
    folderTree_->setHeaderHidden(true);
    folderTree_->setMinimumWidth(280);
    folderTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    folderTree_->setAlternatingRowColors(true);
    folderTree_->setDragEnabled(true);
    folderTree_->setAcceptDrops(true);
    folderTree_->setDropIndicatorShown(true);
    folderTree_->setDragDropMode(QAbstractItemView::InternalMove);
    connect(folderTree_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::refreshList);
    connect(folderTree_, &QTreeWidget::customContextMenuRequested, this, &MainWindow::showTreeContextMenu);

    itemTable_ = new QTableWidget(splitter);
    itemTable_->setColumnCount(8);
    itemTable_->setHorizontalHeaderLabels({QStringLiteral(""), QStringLiteral("名称"), QStringLiteral("类型"), QStringLiteral("网址"), QStringLiteral("测活"), QStringLiteral("状态码"), QStringLiteral("耗时"), QStringLiteral("添加时间")});
    itemTable_->horizontalHeader()->setStretchLastSection(false);
    itemTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    itemTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    itemTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    itemTable_->setColumnWidth(0, 32);
    itemTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    itemTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    itemTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    itemTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    itemTable_->setAlternatingRowColors(true);
    itemTable_->setDragEnabled(true);
    itemTable_->setAcceptDrops(true);
    itemTable_->setDropIndicatorShown(true);
    itemTable_->setDragDropMode(QAbstractItemView::InternalMove);
    connect(itemTable_, &QTableWidget::cellDoubleClicked, this, &MainWindow::openSelectedUrl);
    connect(itemTable_, &QTableWidget::customContextMenuRequested, this, &MainWindow::showTableContextMenu);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);
    setCentralWidget(splitter);
    statusBar()->setStyleSheet("QStatusBar { border-top: 1px solid palette(mid); padding: 4px; }");
}

void MainWindow::refreshTree(const QString& preferredPath)
{
    QString selectedPath = preferredPath.isEmpty() ? currentFolderPath() : preferredPath;
    const QStringList checkedPaths = checkedFolderPaths();

    folderTree_->clear();
    for (const auto& root : document_.roots()) {
        addFolderItem(nullptr, root.get());
    }

    restoreCheckedFolders(checkedPaths);

    QTreeWidgetItem* selectedItem = nullptr;
    while (selectedItem == nullptr && !selectedPath.isEmpty()) {
        selectedItem = findFolderItemByPath(selectedPath);
        if (selectedItem == nullptr) {
            const int slash = selectedPath.lastIndexOf('/');
            selectedPath = slash > 0 ? selectedPath.left(slash) : QString();
        }
    }

    if (selectedItem == nullptr && folderTree_->topLevelItemCount() > 0) {
        selectedItem = folderTree_->topLevelItem(0);
    }
    if (selectedItem != nullptr) {
        for (auto* parent = selectedItem->parent(); parent != nullptr; parent = parent->parent()) {
            parent->setExpanded(true);
        }
        selectedItem->setExpanded(true);
        folderTree_->setCurrentItem(selectedItem);
    }
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
    QSet<int> checkedRows;

    // 收集勾选的行
    for (int row = 0; row < itemTable_->rowCount(); ++row) {
        if (auto* checkItem = itemTable_->item(row, 0)) {
            if (checkItem->checkState() == Qt::Checked) {
                checkedRows.insert(row);
            }
        }
    }

    // 如果有勾选项，优先返回勾选的
    if (!checkedRows.isEmpty()) {
        for (int row : checkedRows) {
            if (auto* item = itemTable_->item(row, 1)) {
                if (auto* node = nodeFromTableItem(item)) {
                    nodes.push_back(node);
                }
            }
        }
        return nodes;
    }

    // 否则返回选中的行
    const auto ranges = itemTable_->selectedRanges();
    QSet<int> rows;
    for (const auto& range : ranges) {
        for (int row = range.topRow(); row <= range.bottomRow(); ++row) {
            rows.insert(row);
        }
    }
    for (int row : rows) {
        if (auto* item = itemTable_->item(row, 1)) {
            if (auto* node = nodeFromTableItem(item)) {
                nodes.push_back(node);
            }
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
    QVector<BookmarkNode*> nodes;
    QSet<BookmarkNode*> seen;
    const auto appendUnique = [&nodes, &seen](const QVector<BookmarkNode*>& source) {
        for (auto* node : source) {
            if (node != nullptr && !seen.contains(node)) {
                seen.insert(node);
                nodes.push_back(node);
            }
        }
    };

    appendUnique(selectedListNodes());
    appendUnique(checkedFolderNodes());
    return filterNestedNodes(nodes);
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
        labels << folder->path();
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

QString MainWindow::currentFolderPath() const
{
    auto* folder = currentFolder();
    return folder == nullptr ? QString() : folder->path();
}

QString MainWindow::nearestExistingFolderPath(QString path) const
{
    while (!path.isEmpty()) {
        for (auto* folder : document_.folders()) {
            if (folder != nullptr && folder->path() == path) {
                return path;
            }
        }
        const int slash = path.lastIndexOf('/');
        path = slash > 0 ? path.left(slash) : QString();
    }
    return {};
}

QStringList MainWindow::checkedFolderPaths() const
{
    QStringList paths;
    for (int i = 0; i < folderTree_->topLevelItemCount(); ++i) {
        collectCheckedFolderPaths(folderTree_->topLevelItem(i), &paths);
    }
    return paths;
}

void MainWindow::restoreCheckedFolders(const QStringList& paths)
{
    QSet<QString> pathSet;
    for (const auto& path : paths) {
        pathSet.insert(path);
    }
    for (const auto& path : pathSet) {
        if (auto* item = findFolderItemByPath(path)) {
            if (item->flags() & Qt::ItemIsUserCheckable) {
                item->setCheckState(0, Qt::Checked);
            }
        }
    }
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

void MainWindow::collectCheckedFolderPaths(QTreeWidgetItem* item, QStringList* paths) const
{
    if (item == nullptr || paths == nullptr) {
        return;
    }
    if ((item->flags() & Qt::ItemIsUserCheckable) && item->checkState(0) == Qt::Checked) {
        if (auto* node = nodeFromItem(item)) {
            paths->append(node->path());
        }
    }
    for (int i = 0; i < item->childCount(); ++i) {
        collectCheckedFolderPaths(item->child(i), paths);
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

QTreeWidgetItem* MainWindow::findFolderItemByPath(const QString& path) const
{
    if (path.isEmpty()) {
        return nullptr;
    }
    for (int i = 0; i < folderTree_->topLevelItemCount(); ++i) {
        if (auto* item = findFolderItemRecursive(folderTree_->topLevelItem(i), path)) {
            return item;
        }
    }
    return nullptr;
}

QTreeWidgetItem* MainWindow::findFolderItemRecursive(QTreeWidgetItem* item, const QString& path) const
{
    if (item == nullptr) {
        return nullptr;
    }
    auto* node = nodeFromItem(item);
    if (node != nullptr && node->path() == path) {
        return item;
    }
    for (int i = 0; i < item->childCount(); ++i) {
        if (auto* match = findFolderItemRecursive(item->child(i), path)) {
            return match;
        }
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

BookmarkNode* MainWindow::nodeFromItem(QTreeWidgetItem* item)
{
    if (item == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<BookmarkNode*>(item->data(0, NodeRole).value<quintptr>());
}

BookmarkNode* MainWindow::nodeFromTableItem(QTableWidgetItem* item)
{
    if (item == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<BookmarkNode*>(item->data(NodeRole).value<quintptr>());
}

void MainWindow::setNodeData(QTreeWidgetItem* item, BookmarkNode* node)
{
    item->setData(0, NodeRole, QVariant::fromValue(reinterpret_cast<quintptr>(node)));
}

void MainWindow::setNodeData(QTableWidgetItem* item, BookmarkNode* node)
{
    item->setData(NodeRole, QVariant::fromValue(reinterpret_cast<quintptr>(node)));
}
