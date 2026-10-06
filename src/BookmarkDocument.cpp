#include "BookmarkDocument.h"

#include "ChromeProfiles.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <QUuid>

#include <algorithm>
#include <filesystem>
#include <iterator>

namespace {
QString absolutePath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

// Resolve existing symlinks, including parent directories of a new Save As file.
QString canonicalPath(const QString& path)
{
    QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty()) return canonical;
    const QString absolute = absolutePath(path);
    const QString parent = info.absolutePath();
    if (parent == absolute) return absolute;
    return QDir(canonicalPath(parent)).filePath(info.fileName());
}

bool samePath(const QString& left, const QString& right)
{
#ifdef Q_OS_WIN
    return left.compare(right, Qt::CaseInsensitive) == 0;
#else
    return left == right;
#endif
}

std::filesystem::path nativePath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(QFile::encodeName(path).constData());
#endif
}

bool sameFile(const QString& left, const QString& right)
{
    if (left.isEmpty() || right.isEmpty()) return false;
    if (samePath(left, right)) return true;
    std::error_code error;
    return std::filesystem::equivalent(nativePath(left), nativePath(right), error) && !error;
}

bool fingerprint(const QString& path, QByteArray* result, QString* error)
{
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("无法读取文件 %1：%2").arg(path, file.errorString());
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || file.error() != QFileDevice::NoError) {
        if (error) *error = QStringLiteral("读取文件失败 %1：%2").arg(path, file.errorString());
        return false;
    }
    *result = hash.result();
    return true;
}

bool validNode(const QJsonObject& object, bool root = false)
{
    const QString type = root && !object.contains("type")
        ? QStringLiteral("folder") : object.value("type").toString();
    if (type != QStringLiteral("folder")) return !root && type == QStringLiteral("url");
    if (object.contains("children") && !object.value("children").isArray()) return false;
    for (const auto& child : object.value("children").toArray()) {
        if (!child.isObject() || !validNode(child.toObject())) return false;
    }
    return true;
}

QString allocateId(QSet<QString>& used, quint64& counter)
{
    QString id;
    do {
        ++counter; // Unsigned wraparound is intentional for a maximum-width Chrome ID.
        if (counter == 0) ++counter;
        id = QString::number(counter);
    } while (used.contains(id));
    used.insert(id);
    return id;
}

void updateRootKey(BookmarkNode* node, const QString& rootKey)
{
    node->rootKey = rootKey;
    for (const auto& child : node->children) updateRootKey(child.get(), rootKey);
}
}

bool BookmarkDocument::load(const QString& filePath, QString* error)
{
    if (error) error->clear();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        if (error) *error = file.errorString();
        return false;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = parseError.error == QJsonParseError::NoError
            ? QStringLiteral("这不是有效的 Chrome Bookmarks 文件") : parseError.errorString();
        return false;
    }
    const QJsonObject object = document.object();
    if (!object.value("roots").isObject()) {
        if (error) *error = QStringLiteral("这不是有效的 Chrome Bookmarks 文件");
        return false;
    }
    const QJsonObject rootsObject = object.value("roots").toObject();
    for (auto it = rootsObject.begin(); it != rootsObject.end(); ++it) {
        if (!it.value().isObject() || !validNode(it.value().toObject(), true)) {
            if (error) *error = QStringLiteral("收藏夹树结构无效：%1").arg(it.key());
            return false;
        }
    }

    // Build the replacement in isolation: even a failed load must retain all old pointers.
    BookmarkDocument replacement;
    replacement.topLevel_ = object;
    replacement.loadRoots();
    replacement.path_ = filePath;
    replacement.sourceAbsolutePath_ = absolutePath(filePath);
    replacement.sourceCanonicalPath_ = canonicalPath(filePath);
    replacement.sourceFingerprint_ = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    replacement.generation_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    replacement.revision_ = revision_ + 1;
    *this = std::move(replacement);
    return true;
}

bool BookmarkDocument::save(const QString& filePath, bool requireChromeClosed, QString* error)
{
    if (error) error->clear();
    const QString requestedPath = filePath.isEmpty() ? path_ : filePath;
    if (requestedPath.isEmpty()) {
        if (error) *error = QStringLiteral("没有保存路径");
        return false;
    }
    if (requireChromeClosed && isChromeRunning()) {
        if (error) *error = QStringLiteral("检测到 Chrome 正在运行。请先关闭 Chrome，再保存收藏夹。");
        return false;
    }

    const QString targetAbsolute = filePath.isEmpty() && !sourceAbsolutePath_.isEmpty()
        ? sourceAbsolutePath_ : absolutePath(requestedPath);
    const QString targetPath = canonicalPath(targetAbsolute);
    const bool sameSource = !sourceFingerprint_.isEmpty()
        && (samePath(targetAbsolute, sourceAbsolutePath_)
            || samePath(targetPath, sourceCanonicalPath_)
            || sameFile(targetAbsolute, sourceAbsolutePath_)
            || sameFile(targetPath, sourceCanonicalPath_));
    const auto conflict = [error]() {
        if (error) *error = QStringLiteral("磁盘上的收藏夹已更改或无法验证，已拒绝覆盖。请重新加载或另存为其他文件。");
        return false;
    };

    const QFileInfo targetInfo(targetPath);
    const bool targetExists = targetInfo.exists() || targetInfo.isSymLink();
    QByteArray targetFingerprint;
    if (sameSource) {
        if (!fingerprint(targetPath, &targetFingerprint, nullptr)
            || targetFingerprint != sourceFingerprint_) return conflict();
    } else if (targetExists && !fingerprint(targetPath, &targetFingerprint, error)) {
        return false;
    }

    if (!QDir().mkpath(targetInfo.absolutePath())) {
        if (error) *error = QStringLiteral("无法创建保存目录：%1").arg(targetInfo.absolutePath());
        return false;
    }

    if (targetExists) {
        const QString backup = targetPath + QStringLiteral(".backup-")
            + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz-"))
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QFile original(targetPath);
        if (!original.copy(backup)) {
            if (error) *error = QStringLiteral("创建备份失败，已拒绝覆盖：%1\n%2").arg(backup, original.errorString());
            return false;
        }
        QByteArray backupFingerprint;
        if (!fingerprint(backup, &backupFingerprint, error)) return false;
        if (backupFingerprint != targetFingerprint) return conflict();
    }

    QJsonObject rootsObject;
    for (const auto& root : roots_) rootsObject.insert(root->rootKey, root->toJson());
    QJsonObject output = topLevel_;
    output.remove("checksum");
    output.insert("roots", rootsObject);
    const QByteArray bytes = QJsonDocument(output).toJson(QJsonDocument::Indented);

    QSaveFile saveFile(targetPath);
    // Never enable direct-write fallback: failures must leave the old target intact.
    if (!saveFile.open(QIODevice::WriteOnly)) {
        if (error) *error = saveFile.errorString();
        return false;
    }
    if (saveFile.write(bytes) != bytes.size()) {
        if (error) *error = QStringLiteral("写入收藏夹失败：%1").arg(saveFile.errorString());
        saveFile.cancelWriting();
        return false;
    }

    // Also detect changes during backup/serialization, and a newly appeared Save As target.
    QByteArray currentFingerprint;
    const QFileInfo currentInfo(targetPath);
    if (!samePath(canonicalPath(targetAbsolute), targetPath)
        || (targetExists && (!fingerprint(targetPath, &currentFingerprint, nullptr)
                              || currentFingerprint != targetFingerprint))
        || (!targetExists && (currentInfo.exists() || currentInfo.isSymLink()))) {
        saveFile.cancelWriting();
        return conflict();
    }
    if (!saveFile.commit()) {
        if (error) *error = QStringLiteral("提交收藏夹失败：%1").arg(saveFile.errorString());
        return false;
    }

    topLevel_ = output;
    path_ = requestedPath;
    sourceAbsolutePath_ = targetAbsolute;
    sourceCanonicalPath_ = targetPath;
    sourceFingerprint_ = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    dirty_ = false;
    return true;
}

QString BookmarkDocument::path() const { return path_; }
bool BookmarkDocument::isDirty() const { return dirty_; }
void BookmarkDocument::setDirty(bool value)
{
    dirty_ = value;
    if (value) ++revision_;
}
quint64 BookmarkDocument::revision() const { return revision_; }
QString BookmarkDocument::generation() const { return generation_; }

const std::vector<std::unique_ptr<BookmarkNode>>& BookmarkDocument::roots() const
{
    return roots_;
}

std::vector<BookmarkNode*> BookmarkDocument::allNodes() const
{
    std::vector<BookmarkNode*> nodes;
    const auto visit = [&nodes](BookmarkNode* root, const auto& self) -> void {
        nodes.push_back(root);
        for (const auto& child : root->children) self(child.get(), self);
    };
    for (const auto& root : roots_) visit(root.get(), visit);
    return nodes;
}

std::vector<BookmarkNode*> BookmarkDocument::folders() const
{
    std::vector<BookmarkNode*> result;
    for (auto* node : allNodes()) {
        if (node->isFolder()) result.push_back(node);
    }
    return result;
}

BookmarkNode* BookmarkDocument::nodeById(const QString& id) const
{
    if (id.isEmpty()) return nullptr;
    BookmarkNode* result = nullptr;
    for (auto* node : allNodes()) {
        if (node->id() == id) {
            if (result != nullptr) return nullptr;
            result = node;
        }
    }
    return result;
}

bool BookmarkDocument::containsNode(const BookmarkNode* node) const
{
    if (node == nullptr) return false;
    const auto nodes = allNodes();
    return std::find(nodes.begin(), nodes.end(), node) != nodes.end();
}

BookmarkNode* BookmarkDocument::addFolder(BookmarkNode* parent, const QString& name)
{
    if (!containsNode(parent) || !parent->isFolder()) return nullptr;
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
    setDirty(true);
    return ptr;
}

BookmarkNode* BookmarkDocument::addBookmark(BookmarkNode* parent, const QString& name, const QString& url)
{
    if (!containsNode(parent) || !parent->isFolder()) return nullptr;
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
    setDirty(true);
    return ptr;
}

bool BookmarkDocument::add(BookmarkNode* source, BookmarkNode* parent, QString* error)
{
    if (error) error->clear();
    if (source == nullptr) {
        if (error) *error = QStringLiteral("没有要添加的项目");
        return false;
    }
    // Imported sources may belong to another tree; the destination may not.
    if (!containsNode(parent) || !parent->isFolder()) {
        if (error) *error = QStringLiteral("目标必须是本文档的文件夹");
        return false;
    }

    quint64 idCounter = maxNumericId();
    QSet<QString> usedIds;
    for (auto* node : allNodes()) usedIds.insert(node->id());
    const auto cloneObject = [&idCounter, &usedIds](BookmarkNode* node, const auto& self) -> QJsonObject {
        QJsonObject object = node->toJson();
        object.insert(QStringLiteral("id"), allocateId(usedIds, idCounter));
        object.insert(QStringLiteral("guid"), QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (node->isFolder()) {
            QJsonArray children;
            for (const auto& child : node->children) children.append(self(child.get(), self));
            object.insert(QStringLiteral("children"), children);
        }
        return object;
    };

    auto child = std::make_unique<BookmarkNode>(cloneObject(source, cloneObject), parent->rootKey, parent);
    parent->children.push_back(std::move(child));
    parent->touch();
    setDirty(true);
    return true;
}

bool BookmarkDocument::rename(BookmarkNode* node, const QString& name, QString* error)
{
    if (error) error->clear();
    if (!containsNode(node) || node->parent == nullptr) {
        if (error) *error = QStringLiteral("不能重命名根收藏夹或其他文档的项目");
        return false;
    }
    if (node->name() == name) return true;
    node->setName(name);
    setDirty(true);
    return true;
}

bool BookmarkDocument::updateUrl(BookmarkNode* node, const QString& url, QString* error)
{
    if (error) error->clear();
    if (!containsNode(node) || !node->isUrl()) {
        if (error) *error = QStringLiteral("只有本文档的书签可以修改网址");
        return false;
    }
    if (node->url() == url) return true;
    node->setUrl(url);
    setDirty(true);
    return true;
}

bool BookmarkDocument::remove(BookmarkNode* node, QString* error)
{
    if (error) error->clear();
    if (!containsNode(node) || node->parent == nullptr || !containsNode(node->parent)) {
        if (error) *error = QStringLiteral("不能删除根收藏夹或其他文档的项目");
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
    setDirty(true);
    return true;
}

bool BookmarkDocument::move(BookmarkNode* node, BookmarkNode* target, QString* error)
{
    return moveNodes({node}, target, -1, error);
}

bool BookmarkDocument::moveNodes(const QVector<BookmarkNode*>& nodes, BookmarkNode* target,
                                 int insertionIndex, QString* error)
{
    if (error) error->clear();
    const auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    const auto treeOrder = allNodes();
    const QSet<BookmarkNode*> members(treeOrder.begin(), treeOrder.end());
    // Check membership BEFORE dereferencing caller-supplied pointers (including stale pointers).
    if (!members.contains(target) || !target->isFolder()) {
        return fail(QStringLiteral("目标必须是本文档的文件夹"));
    }
    const auto childCount = target->children.size();
    if (insertionIndex < -1 || (insertionIndex >= 0 && static_cast<size_t>(insertionIndex) > childCount)) {
        return fail(QStringLiteral("插入位置无效"));
    }
    const size_t slot = insertionIndex == -1 ? childCount : static_cast<size_t>(insertionIndex);

    QSet<BookmarkNode*> selected;
    for (auto* node : nodes) {
        if (!members.contains(node)) return fail(QStringLiteral("移动项目不属于本文档"));
        if (node->parent == nullptr) return fail(QStringLiteral("不能移动 Chrome 根收藏夹"));
        if (!members.contains(node->parent)
            || std::none_of(node->parent->children.begin(), node->parent->children.end(),
                            [node](const auto& child) { return child.get() == node; })) {
            return fail(QStringLiteral("移动项目的父文件夹无效"));
        }
        if (node == target || isDescendant(target, node)) {
            return fail(QStringLiteral("不能移动到自身或自己的子文件夹"));
        }
        selected.insert(node);
    }

    std::vector<BookmarkNode*> ordered;
    for (auto* node : treeOrder) {
        if (!selected.contains(node)) continue;
        bool covered = false;
        for (auto* parent = node->parent; parent != nullptr; parent = parent->parent) {
            if (selected.contains(parent)) {
                covered = true;
                break;
            }
        }
        if (!covered) ordered.push_back(node);
    }
    if (ordered.empty()) return true;
    const QSet<BookmarkNode*> moving(ordered.begin(), ordered.end());

    size_t adjustedSlot = slot;
    std::vector<BookmarkNode*> result;
    result.reserve(childCount + ordered.size());
    for (size_t i = 0; i < childCount; ++i) {
        auto* child = target->children[i].get();
        if (moving.contains(child)) {
            if (i < slot) --adjustedSlot;
        } else {
            result.push_back(child);
        }
    }
    result.insert(result.begin() + static_cast<std::ptrdiff_t>(adjustedSlot), ordered.begin(), ordered.end());
    if (result.size() == childCount
        && std::equal(result.begin(), result.end(), target->children.begin(),
                      [](const auto* node, const auto& owned) { return node == owned.get(); })) {
        return true;
    }

    // All validation and allocations precede detachment; nothing can fail midway with a false return.
    std::vector<std::unique_ptr<BookmarkNode>> owned;
    owned.reserve(ordered.size());
    target->children.reserve(result.size());
    QSet<BookmarkNode*> parents;
    parents.insert(target);
    for (auto* node : ordered) parents.insert(node->parent);
    for (auto* node : ordered) owned.push_back(takeFromParent(node));
    for (const auto& node : owned) {
        node->parent = target;
        updateRootKey(node.get(), target->rootKey);
    }
    target->children.insert(target->children.begin() + static_cast<std::ptrdiff_t>(adjustedSlot),
                            std::make_move_iterator(owned.begin()), std::make_move_iterator(owned.end()));
    for (auto* parent : parents) parent->touch();
    setDirty(true);
    return true;
}

void BookmarkDocument::loadRoots()
{
    roots_.clear();
    const QJsonObject rootsObject = topLevel_.value("roots").toObject();
    const QStringList preferred = {"bookmark_bar", "other", "synced"};
    QStringList keys;
    for (const auto& key : preferred) {
        if (rootsObject.contains(key)) keys.append(key);
    }
    for (const auto& key : rootsObject.keys()) {
        if (!keys.contains(key)) keys.append(key);
    }
    for (const auto& key : keys) {
        QJsonObject object = rootsObject.value(key).toObject();
        if (!object.contains("type")) object.insert("type", "folder");
        if (!object.contains("name") || object.value("name").toString().isEmpty()) {
            object.insert("name", rootLabel(key));
        }
        roots_.push_back(std::make_unique<BookmarkNode>(object, key, nullptr));
    }
}

QString BookmarkDocument::nextId() const
{
    quint64 counter = maxNumericId();
    QSet<QString> usedIds;
    for (auto* node : allNodes()) usedIds.insert(node->id());
    return allocateId(usedIds, counter);
}

quint64 BookmarkDocument::maxNumericId() const
{
    quint64 value = 0;
    for (auto* node : allNodes()) {
        bool ok = false;
        const quint64 id = node->id().toULongLong(&ok);
        if (ok) value = std::max(value, id);
    }
    return value;
}

std::unique_ptr<BookmarkNode> BookmarkDocument::takeFromParent(BookmarkNode* node)
{
    if (node == nullptr || node->parent == nullptr) return nullptr;
    auto& siblings = node->parent->children;
    const auto it = std::find_if(siblings.begin(), siblings.end(), [node](const auto& item) {
        return item.get() == node;
    });
    if (it == siblings.end()) return nullptr;
    auto owned = std::move(*it);
    siblings.erase(it);
    return owned;
}

bool BookmarkDocument::isDescendant(BookmarkNode* candidate, BookmarkNode* ancestor) const
{
    for (auto* current = candidate ? candidate->parent : nullptr; current != nullptr; current = current->parent) {
        if (current == ancestor) return true;
    }
    return false;
}
