#include "MainWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
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
    profiles_ = discoverChromeProfiles();
    profileCombo_->clear();
    for (const auto& profile : profiles_) {
        profileCombo_->addItem(profile.label());
    }
    setStatus(profiles_.isEmpty()
        ? QStringLiteral("未发现 Chrome Profile，可手动打开 Bookmarks 文件")
        : QStringLiteral("发现 %1 个 Chrome Profile").arg(profiles_.size()));
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
    QString error;
    if (!document_.save(target, true, &error)) {
        QMessageBox::critical(this, QStringLiteral("保存失败"), error);
        return;
    }
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
    refreshTree();
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
    refreshTree();
}

void MainWindow::renameSelected()
{
    auto nodes = selectedListNodes();
    BookmarkNode* node = nodes.size() == 1 ? nodes[0] : currentFolder();
    if (node == nullptr) {
        return;
    }
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
    refreshTree();
}

void MainWindow::deleteSelected()
{
    auto nodes = selectedListNodes();
    if (nodes.isEmpty()) {
        auto* folder = currentFolder();
        if (folder != nullptr) {
            nodes.push_back(folder);
        }
    }
    if (nodes.isEmpty()) {
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("删除"), QStringLiteral("确认删除 %1 个项目？").arg(nodes.size())) != QMessageBox::Yes) {
        return;
    }
    QString error;
    for (auto* node : nodes) {
        if (!document_.remove(node, &error)) {
            QMessageBox::warning(this, QStringLiteral("删除失败"), error);
            break;
        }
    }
    refreshTree();
}

void MainWindow::moveSelected()
{
    const auto nodes = selectedListNodes();
    if (nodes.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("移动"), QStringLiteral("请在右侧列表选择要移动的项目"));
        return;
    }
    auto* target = chooseFolder();
    if (target == nullptr) {
        return;
    }
    QString error;
    for (auto* node : nodes) {
        if (!document_.move(node, target, &error)) {
            QMessageBox::warning(this, QStringLiteral("移动失败"), error);
            break;
        }
    }
    refreshTree();
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
    checkButton_->setEnabled(false);
    setStatus(QStringLiteral("开始测活：%1 个网址").arg(nodes.size()));
    health_.check(nodes);
}

void MainWindow::onHealthResult(BookmarkNode* node, const HealthResult& result)
{
    healthResults_.insert(node, result);
    refreshList();
}

void MainWindow::onHealthFinished(int total, int failed)
{
    checkButton_->setEnabled(true);
    setStatus(QStringLiteral("测活完成：已检测 %1 个，异常 %2 个").arg(total).arg(failed));
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
    connect(folderTree_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::refreshList);

    itemTable_ = new QTableWidget(splitter);
    itemTable_->setColumnCount(7);
    itemTable_->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("类型"), QStringLiteral("网址"), QStringLiteral("测活"), QStringLiteral("状态码"), QStringLiteral("耗时"), QStringLiteral("添加时间")});
    itemTable_->horizontalHeader()->setStretchLastSection(false);
    itemTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    itemTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    itemTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    itemTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    itemTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(itemTable_, &QTableWidget::cellDoubleClicked, this, &MainWindow::openSelectedUrl);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 4);
    setCentralWidget(splitter);
    statusBar();
}

void MainWindow::refreshTree()
{
    folderTree_->clear();
    for (const auto& root : document_.roots()) {
        addFolderItem(nullptr, root.get());
    }
    if (folderTree_->topLevelItemCount() > 0) {
        auto* first = folderTree_->topLevelItem(0);
        first->setExpanded(true);
        folderTree_->setCurrentItem(first);
    }
    refreshList();
}

void MainWindow::addFolderItem(QTreeWidgetItem* parentItem, BookmarkNode* node)
{
    auto* item = parentItem == nullptr ? new QTreeWidgetItem(folderTree_) : new QTreeWidgetItem(parentItem);
    item->setText(0, node->name());
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

void MainWindow::setStatus(const QString& text)
{
    statusBar()->showMessage(text);
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
