#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
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
#include <QSet>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVariant>
#include <QAbstractItemView>

namespace {
constexpr int NodeRole = Qt::UserRole + 1;

QString codeText(int code)
{
    return code > 0 ? QString::number(code) : QString();
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    buildUi();
    connect(&health_, &HealthChecker::resultReady, this, &MainWindow::onHealthResult);
    connect(&health_, &HealthChecker::finished, this, &MainWindow::onHealthFinished);
    reloadProfiles();
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

void MainWindow::loadSelectedProfile()
{
    const int index = profileCombo_->currentIndex();
    if (index < 0 || index >= profiles_.size()) {
        return;
    }
    QString error;
    if (!document_.load(profiles_[index].bookmarksPath, &error)) {
        QMessageBox::critical(this, QStringLiteral("打开失败"), error);
        return;
    }
    healthResults_.clear();
    refreshTree();
    setStatus(QStringLiteral("已打开：%1").arg(document_.path()));
}

void MainWindow::openBookmarksFile()
{
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
    healthResults_.clear();
    refreshTree();
    setStatus(QStringLiteral("已打开：%1").arg(document_.path()));
}

void MainWindow::saveBookmarks()
{
    QString target = document_.path();
    if (target.isEmpty()) {
        target = QFileDialog::getSaveFileName(this, QStringLiteral("保存 Bookmarks 文件"), QStringLiteral("Bookmarks"));
        if (target.isEmpty()) {
            return;
        }
    }

    showProgress(QStringLiteral("保存"), QStringLiteral("准备保存收藏夹..."), 3);
    updateProgress(QStringLiteral("正在检查 Chrome 状态..."), 1);
    QString error;
    updateProgress(QStringLiteral("正在备份并写入 Bookmarks 文件..."), 2);
    if (!document_.save(target, true, &error)) {
        closeProgress();
        QMessageBox::critical(this, QStringLiteral("保存失败"), error);
        return;
    }
    updateProgress(QStringLiteral("保存完成"), 3);
    closeProgress();
    setStatus(QStringLiteral("已保存并自动备份：%1").arg(target));
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
        auto* nameItem = new QTableWidgetItem(node->name());
        setNodeData(nameItem, node);
        itemTable_->setItem(row, 0, nameItem);
        itemTable_->setItem(row, 1, new QTableWidgetItem(node->displayType()));
        itemTable_->setItem(row, 2, new QTableWidgetItem(node->url()));

        const auto result = healthResults_.value(node);
        itemTable_->setItem(row, 3, new QTableWidgetItem(result.status));
        itemTable_->setItem(row, 4, new QTableWidgetItem(codeText(result.code)));
        itemTable_->setItem(row, 5, new QTableWidgetItem(result.elapsedMs > 0 ? QStringLiteral("%1 ms").arg(result.elapsedMs) : QString()));
        itemTable_->setItem(row, 6, new QTableWidgetItem(node->formattedDateAdded()));
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
    if (nodes.size() != 1 || !nodes[0]->isUrl()) {
        return;
    }
    QDesktopServices::openUrl(QUrl(nodes[0]->url()));
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
    auto* deleteAction = menu.addAction(QStringLiteral("删除"), this, &MainWindow::deleteSelected);
    auto* moveAction = menu.addAction(QStringLiteral("移动到"), this, &MainWindow::moveSelected);
    renameAction->setEnabled(!folder->isRoot() || selectedOperationNodes().size() == 1);
    deleteAction->setEnabled(!folder->isRoot() || !selectedOperationNodes().isEmpty());
    moveAction->setEnabled(!folder->isRoot() || !selectedOperationNodes().isEmpty());

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
    menu.addAction(QStringLiteral("新建文件夹"), this, &MainWindow::newFolder);
    menu.addAction(QStringLiteral("新建书签"), this, &MainWindow::newBookmark);
    menu.addSeparator();
    auto* renameAction = menu.addAction(QStringLiteral("重命名"), this, &MainWindow::renameSelected);
    auto* deleteAction = menu.addAction(QStringLiteral("删除"), this, &MainWindow::deleteSelected);
    auto* moveAction = menu.addAction(QStringLiteral("移动到"), this, &MainWindow::moveSelected);
    const bool hasOperationNodes = !selectedOperationNodes().isEmpty();
    renameAction->setEnabled(selectedOperationNodes().size() <= 1);
    deleteAction->setEnabled(hasOperationNodes || currentFolder() != nullptr);
    moveAction->setEnabled(hasOperationNodes);
    menu.addSeparator();
    menu.addAction(QStringLiteral("网址测活"), this, &MainWindow::checkUrls);

    menu.exec(itemTable_->viewport()->mapToGlobal(position));
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Chrome Bookmark Explorer"));
    resize(1180, 720);

    auto* toolbar = addToolBar(QStringLiteral("工具栏"));
    toolbar->setMovable(false);

    toolbar->addWidget(new QLabel(QStringLiteral("Profile ")));
    profileCombo_ = new QComboBox(this);
    profileCombo_->setMinimumWidth(280);
    toolbar->addWidget(profileCombo_);
    connect(profileCombo_, qOverload<int>(&QComboBox::activated), this, [this](int) { loadSelectedProfile(); });

    toolbar->addAction(QStringLiteral("刷新"), this, &MainWindow::reloadProfiles);
    toolbar->addAction(QStringLiteral("打开文件"), this, &MainWindow::openBookmarksFile);
    toolbar->addAction(QStringLiteral("保存"), this, &MainWindow::saveBookmarks);
    toolbar->addSeparator();
    toolbar->addAction(QStringLiteral("新建文件夹"), this, &MainWindow::newFolder);
    toolbar->addAction(QStringLiteral("新建书签"), this, &MainWindow::newBookmark);
    toolbar->addAction(QStringLiteral("重命名"), this, &MainWindow::renameSelected);
    toolbar->addAction(QStringLiteral("删除"), this, &MainWindow::deleteSelected);
    toolbar->addAction(QStringLiteral("移动到"), this, &MainWindow::moveSelected);
    toolbar->addSeparator();

    includeSubfolders_ = new QCheckBox(QStringLiteral("含子文件夹"), this);
    includeSubfolders_->setChecked(true);
    toolbar->addWidget(includeSubfolders_);

    toolbar->addWidget(new QLabel(QStringLiteral("并发数 ")));
    concurrencySpin_ = new QSpinBox(this);
    concurrencySpin_->setRange(1, 128);
    concurrencySpin_->setValue(health_.maxConcurrent());
    concurrencySpin_->setToolTip(QStringLiteral("同时测活的网址数量；数值越大速度越快，但也更容易触发站点限速"));
    concurrencySpin_->setMaximumWidth(72);
    toolbar->addWidget(concurrencySpin_);

    checkButton_ = new QPushButton(QStringLiteral("网址测活"), this);
    toolbar->addWidget(checkButton_);
    connect(checkButton_, &QPushButton::clicked, this, &MainWindow::checkUrls);

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("搜索 ")));
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setMaximumWidth(260);
    toolbar->addWidget(searchEdit_);
    connect(searchEdit_, &QLineEdit::textChanged, this, &MainWindow::refreshList);

    auto* splitter = new QSplitter(this);
    folderTree_ = new QTreeWidget(splitter);
    folderTree_->setHeaderHidden(true);
    folderTree_->setMinimumWidth(260);
    folderTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(folderTree_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::refreshList);
    connect(folderTree_, &QTreeWidget::customContextMenuRequested, this, &MainWindow::showTreeContextMenu);

    itemTable_ = new QTableWidget(splitter);
    itemTable_->setColumnCount(7);
    itemTable_->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("类型"), QStringLiteral("网址"), QStringLiteral("测活"), QStringLiteral("状态码"), QStringLiteral("耗时"), QStringLiteral("添加时间")});
    itemTable_->horizontalHeader()->setStretchLastSection(false);
    itemTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    itemTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    itemTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    itemTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    itemTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    itemTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(itemTable_, &QTableWidget::cellDoubleClicked, this, &MainWindow::openSelectedUrl);
    connect(itemTable_, &QTableWidget::customContextMenuRequested, this, &MainWindow::showTableContextMenu);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);
    setCentralWidget(splitter);
    statusBar();
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
}

void MainWindow::addFolderItem(QTreeWidgetItem* parentItem, BookmarkNode* node)
{
    auto* item = parentItem == nullptr ? new QTreeWidgetItem(folderTree_) : new QTreeWidgetItem(parentItem);
    item->setText(0, node->name());
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
    const auto ranges = itemTable_->selectedRanges();
    QSet<int> rows;
    for (const auto& range : ranges) {
        for (int row = range.topRow(); row <= range.bottomRow(); ++row) {
            rows.insert(row);
        }
    }
    for (int row : rows) {
        if (auto* item = itemTable_->item(row, 0)) {
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
