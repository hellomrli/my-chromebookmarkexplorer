#include "BookmarkDocument.h"

#include "ChromeProfiles.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QStringList>
#include <QUuid>

#include <algorithm>
#include <stdexcept>

bool BookmarkDocument::load(const QString& filePath, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = parseError.errorString();
        return false;
    }

    topLevel_ = document.object();
    if (!topLevel_.contains("roots") || !topLevel_.value("roots").isObject()) {
        if (error) *error = QStringLiteral("这不是有效的 Chrome Bookmarks 文件");
        return false;
    }

    path_ = filePath;
    loadRoots();
    dirty_ = false;
    return true;
}

bool BookmarkDocument::save(const QString& filePath, bool requireChromeClosed, QString* error)
{
    const QString targetPath = filePath.isEmpty() ? path_ : filePath;
    if (targetPath.isEmpty()) {
        if (error) *error = QStringLiteral("没有保存路径");
        return false;
    }
    if (requireChromeClosed && isChromeRunning()) {
        if (error) *error = QStringLiteral("检测到 Chrome 正在运行。请先关闭 Chrome，再保存收藏夹。");
        return false;
    }

    QFileInfo info(targetPath);
    QDir().mkpath(info.absolutePath());

    if (QFile::exists(targetPath)) {
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        const QString backup = info.absolutePath() + QDir::separator() + info.fileName()
            + QStringLiteral(".backup-") + stamp;
        QFile::copy(targetPath, backup);
    }

    QJsonObject rootsObject;
    for (const auto& root : roots_) {
        rootsObject.insert(root->rootKey, root->toJson());
    }

    QJsonObject output = topLevel_;
    output.remove("checksum");
    output.insert("roots", rootsObject);

    QSaveFile saveFile(targetPath);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = saveFile.errorString();
        return false;
    }
    saveFile.write(QJsonDocument(output).toJson(QJsonDocument::Indented));
    if (!saveFile.commit()) {
        if (error) *error = saveFile.errorString();
        return false;
    }

    topLevel_ = output;
    path_ = targetPath;
    dirty_ = false;
    return true;
}

QString BookmarkDocument::path() const { return path_; }
bool BookmarkDocument::isDirty() const { return dirty_; }
void BookmarkDocument::setDirty(bool value) { dirty_ = value; }

const std::vector<std::unique_ptr<BookmarkNode>>& BookmarkDocument::roots() const
{
    return roots_;
}

std::vector<BookmarkNode*> BookmarkDocument::allNodes() const
{
    std::vector<BookmarkNode*> nodes;
    const auto visit = [&nodes](BookmarkNode* root, const auto& self) -> void {
        nodes.push_back(root);
        for (const auto& child : root->children) {
            self(child.get(), self);
        }
    };
    for (const auto& root : roots_) {
        visit(root.get(), visit);
    }
    return nodes;
}

std::vector<BookmarkNode*> BookmarkDocument::folders() const
{
    std::vector<BookmarkNode*> result;
    for (auto* node : allNodes()) {
        if (node->isFolder()) {
            result.push_back(node);
        }
    }
    return result;
}

BookmarkNode* BookmarkDocument::addFolder(BookmarkNode* parent, const QString& name)
{
    if (parent == nullptr || !parent->isFolder()) {
        return nullptr;
    }
    QJsonObject object;
    object.insert("children", QJsonArray());
    object.insert("date_added", currentChromeTime());
    object.insert("date_modified", currentChromeTime());
    object.insert("guid", QUuid::createUuid().toString(QUuid::WithoutBraces));
    object.insert("id", nextId());
    object.insert("name", name);
    object.insert("type", "folder");

    auto child = std::make_unique<BookmarkNode>(object, parent->rootKey, parent);
    auto* ptr = child.get();
    parent->children.push_back(std::move(child));
    parent->touch();
    dirty_ = true;
    return ptr;
}

BookmarkNode* BookmarkDocument::addBookmark(BookmarkNode* parent, const QString& name, const QString& url)
{
    if (parent == nullptr || !parent->isFolder()) {
        return nullptr;
    }
    QJsonObject object;
    object.insert("date_added", currentChromeTime());
    object.insert("guid", QUuid::createUuid().toString(QUuid::WithoutBraces));
    object.insert("id", nextId());
    object.insert("name", name);
    object.insert("type", "url");
    object.insert("url", url);

    auto child = std::make_unique<BookmarkNode>(object, parent->rootKey, parent);
    auto* ptr = child.get();
    parent->children.push_back(std::move(child));
    parent->touch();
    dirty_ = true;
    return ptr;
}

bool BookmarkDocument::rename(BookmarkNode* node, const QString& name, QString* error)
{
    if (node == nullptr || node->isRoot()) {
        if (error) *error = QStringLiteral("不能重命名 Chrome 根收藏夹");
        return false;
    }
    node->setName(name);
    dirty_ = true;
    return true;
}

bool BookmarkDocument::updateUrl(BookmarkNode* node, const QString& url, QString* error)
{
    if (node == nullptr || !node->isUrl()) {
        if (error) *error = QStringLiteral("只有书签可以修改网址");
        return false;
    }
    node->setUrl(url);
    dirty_ = true;
    return true;
}

bool BookmarkDocument::remove(BookmarkNode* node, QString* error)
{
    if (node == nullptr || node->parent == nullptr) {
        if (error) *error = QStringLiteral("不能删除 Chrome 根收藏夹");
        return false;
    }
    auto& siblings = node->parent->children;
    const auto it = std::find_if(siblings.begin(), siblings.end(), [node](const auto& item) {
        return item.get() == node;
    });
    if (it == siblings.end()) {
        if (error) *error = QStringLiteral("没有找到要删除的项目");
        return false;
    }
    node->parent->touch();
    siblings.erase(it);
    dirty_ = true;
    return true;
}

bool BookmarkDocument::move(BookmarkNode* node, BookmarkNode* target, QString* error)
{
    if (node == nullptr || node->parent == nullptr) {
        if (error) *error = QStringLiteral("不能移动 Chrome 根收藏夹");
        return false;
    }
    if (target == nullptr || !target->isFolder()) {
        if (error) *error = QStringLiteral("目标必须是文件夹");
        return false;
    }
    if (node == target || isDescendant(target, node)) {
        if (error) *error = QStringLiteral("不能移动到自身或自己的子文件夹");
        return false;
    }

    BookmarkNode* oldParent = node->parent;
    auto owned = takeFromParent(node);
    if (!owned) {
        if (error) *error = QStringLiteral("移动失败");
        return false;
    }
    owned->parent = target;
    owned->rootKey = target->rootKey;
    target->children.push_back(std::move(owned));
    oldParent->touch();
    target->touch();
    dirty_ = true;
    return true;
}

void BookmarkDocument::loadRoots()
{
    roots_.clear();
    const QJsonObject rootsObject = topLevel_.value("roots").toObject();
    const QStringList preferred = {"bookmark_bar", "other", "synced"};
    QStringList keys;
    for (const auto& key : preferred) {
        if (rootsObject.contains(key)) {
            keys.append(key);
        }
    }
    for (const auto& key : rootsObject.keys()) {
        if (!keys.contains(key)) {
            keys.append(key);
        }
    }

    for (const auto& key : keys) {
        if (!rootsObject.value(key).isObject()) {
            continue;
        }
        QJsonObject object = rootsObject.value(key).toObject();
        if (!object.contains("type")) {
            object.insert("type", "folder");
        }
        if (!object.contains("name") || object.value("name").toString().isEmpty()) {
            object.insert("name", rootLabel(key));
        }
        roots_.push_back(std::make_unique<BookmarkNode>(object, key, nullptr));
    }
}

QString BookmarkDocument::nextId() const
{
    return QString::number(maxNumericId() + 1);
}

int BookmarkDocument::maxNumericId() const
{
    int value = 0;
    for (auto* node : allNodes()) {
        bool ok = false;
        const int id = node->id().toInt(&ok);
        if (ok) {
            value = std::max(value, id);
        }
    }
    return value;
}

std::unique_ptr<BookmarkNode> BookmarkDocument::takeFromParent(BookmarkNode* node)
{
    if (node == nullptr || node->parent == nullptr) {
        return nullptr;
    }
    auto& siblings = node->parent->children;
    const auto it = std::find_if(siblings.begin(), siblings.end(), [node](const auto& item) {
        return item.get() == node;
    });
    if (it == siblings.end()) {
        return nullptr;
    }
    auto owned = std::move(*it);
    siblings.erase(it);
    return owned;
}

bool BookmarkDocument::isDescendant(BookmarkNode* candidate, BookmarkNode* ancestor) const
{
    BookmarkNode* current = candidate ? candidate->parent : nullptr;
    while (current != nullptr) {
        if (current == ancestor) {
            return true;
        }
        current = current->parent;
    }
    return false;
}
