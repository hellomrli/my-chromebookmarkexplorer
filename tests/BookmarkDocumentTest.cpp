#include "BookmarkDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <filesystem>
#include <limits>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {
QJsonObject bookmark(const QString& id)
{
    return {{"id", id}, {"guid", QStringLiteral("guid-") + id}, {"name", id},
            {"type", "url"}, {"url", QStringLiteral("https://example.test/") + id},
            {"date_added", "13300000000000000"},
            {"unknown_node", QJsonObject{{"nested", QJsonArray{1, "keep", true}}}}};
}

QJsonObject folder(const QString& id, const QJsonArray& children)
{
    return {{"id", id}, {"guid", QStringLiteral("guid-") + id}, {"name", id},
            {"type", "folder"}, {"children", children}, {"date_modified", "13300000000000000"},
            {"unknown_folder", QJsonObject{{"keep", true}}}};
}

QJsonObject sample()
{
    QJsonArray children;
    for (int i = 10; i <= 15; ++i) children.append(bookmark(QString::number(i)));
    return {{"version", 1}, {"checksum", "stale-checksum"},
            {"unknown_top", QJsonObject{{"array", QJsonArray{false, 42, "keep"}}}},
            {"roots", QJsonObject{
                {"bookmark_bar", folder("1", children)},
                {"other", folder("2", QJsonArray{
                    folder("20", QJsonArray{folder("21", QJsonArray{bookmark("22")})}),
                    bookmark("23")})},
                {"synced", folder("3", {})},
                {"custom_root", folder("4", {})}}}};
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush();
}

bool writeSample(const QString& path)
{
    return writeBytes(path, QJsonDocument(sample()).toJson());
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

QJsonObject snapshot(const BookmarkDocument& document)
{
    QJsonObject roots;
    for (const auto& root : document.roots()) roots.insert(root->rootKey, root->toJson());
    return roots;
}

QStringList childIds(const BookmarkNode* parent)
{
    QStringList ids;
    for (const auto& child : parent->children) ids.append(child->id());
    return ids;
}

QStringList backups(const QString& path)
{
    const QFileInfo info(path);
    return QDir(info.absolutePath()).entryList({info.fileName() + ".backup-*"}, QDir::Files, QDir::Name);
}

std::filesystem::path nativePath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(QFile::encodeName(path).constData());
#endif
}
}

class BookmarkDocumentTest final : public QObject {
    Q_OBJECT

private slots:
    void sameDirectoryMoves_data()
    {
        QTest::addColumn<QStringList>("selection");
        QTest::addColumn<int>("slot");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("backward-to-front") << QStringList{"12", "11"} << 0 << QStringList{"11", "12", "10", "13", "14", "15"};
        QTest::newRow("forward-before-last") << QStringList{"12", "11"} << 5 << QStringList{"10", "13", "14", "11", "12", "15"};
        QTest::newRow("append-default") << QStringList{"11", "12"} << -1 << QStringList{"10", "13", "14", "15", "11", "12"};
        QTest::newRow("append-explicit") << QStringList{"11", "12"} << 6 << QStringList{"10", "13", "14", "15", "11", "12"};
        QTest::newRow("head-to-tail") << QStringList{"11", "10"} << 6 << QStringList{"12", "13", "14", "15", "10", "11"};
        QTest::newRow("tail-to-head") << QStringList{"15", "14"} << 0 << QStringList{"14", "15", "10", "11", "12", "13"};
        QTest::newRow("disjoint-middle") << QStringList{"14", "11"} << 3 << QStringList{"10", "12", "11", "14", "13", "15"};
        QTest::newRow("disjoint-append") << QStringList{"14", "11"} << -1 << QStringList{"10", "12", "13", "15", "11", "14"};
        QTest::newRow("duplicate-selection") << QStringList{"12", "11", "12", "11"} << 0 << QStringList{"11", "12", "10", "13", "14", "15"};
    }

    void sameDirectoryMoves()
    {
        QFETCH(QStringList, selection);
        QFETCH(int, slot);
        QFETCH(QStringList, expected);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("Bookmarks");
        QVERIFY(writeSample(path));
        BookmarkDocument document;
        QVERIFY(document.load(path));
        auto* parent = document.nodeById("1");
        QVector<BookmarkNode*> nodes;
        for (const auto& id : selection) nodes.append(document.nodeById(id));
        const quint64 revision = document.revision();
        const QString generation = document.generation();
        QString error;
        QVERIFY2(document.moveNodes(nodes, parent, slot, &error), qPrintable(error));
        QCOMPARE(childIds(parent), expected);
        QCOMPARE(document.revision(), revision + 1);
        QCOMPARE(document.generation(), generation);
        QVERIFY(document.isDirty());
        for (auto* node : nodes) {
            QCOMPARE(document.nodeById(node->id()), node);
            QCOMPARE(node->parent, parent);
        }
    }

    void noOpPreservesState_data()
    {
        QTest::addColumn<QStringList>("selection");
        QTest::addColumn<int>("slot");
        QTest::newRow("before-block") << QStringList{"12", "11"} << 1;
        QTest::newRow("inside-block") << QStringList{"11", "12"} << 2;
        QTest::newRow("after-block") << QStringList{"11", "12"} << 3;
        QTest::newRow("last-item") << QStringList{"15"} << -1;
        QTest::newRow("empty") << QStringList{} << 0;
        QTest::newRow("whole-folder-reversed") << QStringList{"15", "14", "13", "12", "11", "10"} << -1;
    }

    void noOpPreservesState()
    {
        QFETCH(QStringList, selection);
        QFETCH(int, slot);
        QTemporaryDir dir;
        const QString path = dir.filePath("Bookmarks");
        QVERIFY(writeSample(path));
        BookmarkDocument document;
        QVERIFY(document.load(path));
        QVector<BookmarkNode*> nodes;
        for (const auto& id : selection) nodes.append(document.nodeById(id));
        for (const bool dirty : {false, true}) {
            document.setDirty(dirty);
            const auto before = snapshot(document);
            const auto revision = document.revision();
            QString error = "old error";
            QVERIFY(document.moveNodes(nodes, document.nodeById("1"), slot, &error));
            QVERIFY(error.isEmpty());
            QCOMPARE(document.isDirty(), dirty);
            QCOMPARE(document.revision(), revision);
            QCOMPARE(snapshot(document), before);
        }
    }

    void invalidBatchIsAtomic()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("Bookmarks");
        QVERIFY(writeSample(path));
        BookmarkDocument document;
        BookmarkDocument foreign;
        QVERIFY(document.load(path));
        QVERIFY(foreign.load(path));
        auto* bar = document.nodeById("1");
        auto* a = document.nodeById("10");
        auto* nested = document.nodeById("20");
        auto* inner = document.nodeById("21");
        struct Attempt { QVector<BookmarkNode*> nodes; BookmarkNode* target; int slot; };
        const std::vector<Attempt> attempts = {
            {{a, document.nodeById("2")}, bar, -1},
            {{a, document.nodeById("4")}, bar, -1},
            {{a, foreign.nodeById("11")}, bar, -1},
            {{a, nullptr}, bar, -1},
            {{a}, foreign.nodeById("2"), -1},
            {{a}, nullptr, -1},
            {{a}, document.nodeById("11"), -1},
            {{a, nested}, nested, -1},
            {{a, nested}, inner, -1},
            {{a, nested, inner}, inner, -1},
            {{a}, bar, -2},
            {{a}, bar, 7}
        };
        for (const bool dirty : {false, true}) {
            document.setDirty(dirty);
            const auto before = snapshot(document);
            const auto foreignBefore = snapshot(foreign);
            const auto revision = document.revision();
            for (const auto& attempt : attempts) {
                QString error;
                QVERIFY(!document.moveNodes(attempt.nodes, attempt.target, attempt.slot, &error));
                QVERIFY(!error.isEmpty());
                QCOMPARE(snapshot(document), before);
                QCOMPARE(snapshot(foreign), foreignBefore);
                QCOMPARE(document.revision(), revision);
                QCOMPARE(document.isDirty(), dirty);
                QCOMPARE(a->parent, bar);
                QCOMPARE(inner->parent, nested);
            }
        }
    }

    void ancestorFilteringCrossRootAndRawPreservation()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("Bookmarks");
        QVERIFY(writeSample(path));
        BookmarkDocument document;
        QVERIFY(document.load(path));
        auto* bar = document.nodeById("1");
        auto* a = document.nodeById("10");
        auto* nested = document.nodeById("20");
        auto* inner = document.nodeById("21");
        auto* leaf = document.nodeById("22");
        const auto nestedRaw = nested->raw;
        const auto innerRaw = inner->raw;
        const auto leafRaw = leaf->raw;
        const auto revision = document.revision();
        QVERIFY(document.moveNodes({leaf, nested, a, inner, nested}, bar, 2));
        QCOMPARE(childIds(bar), (QStringList{"11", "10", "20", "12", "13", "14", "15"}));
        QCOMPARE(childIds(document.nodeById("2")), (QStringList{"23"}));
        QCOMPARE(nested->parent, bar);
        QCOMPARE(inner->parent, nested);
        QCOMPARE(leaf->parent, inner);
        for (auto* node : {nested, inner, leaf}) {
            QCOMPARE(node->rootKey, QStringLiteral("bookmark_bar"));
            QCOMPARE(document.nodeById(node->id()), node);
            QCOMPARE(node->guid(), QStringLiteral("guid-") + node->id());
        }
        QCOMPARE(nested->raw, nestedRaw);
        QCOMPARE(inner->raw, innerRaw);
        QCOMPARE(leaf->raw, leafRaw);
        QCOMPARE(document.revision(), revision + 1);
    }

    void treeOrderAcrossRoots()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("Bookmarks");
        QVERIFY(writeSample(path));
        BookmarkDocument document;
        QVERIFY(document.load(path));
        auto* target = document.nodeById("3");
        QVERIFY(document.moveNodes({document.nodeById("23"), document.nodeById("12"),
                                    document.nodeById("22"), document.nodeById("10")}, target));
        QCOMPARE(childIds(target), (QStringList{"10", "12", "22", "23"}));
        for (const auto& child : target->children) {
            QCOMPARE(child->rootKey, QStringLiteral("synced"));
            QCOMPARE(child->parent, target);
        }
    }

    void revisionIdentityAndOtherMutations()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("Bookmarks");
        QVERIFY(writeSample(path));
        BookmarkDocument document;
        QCOMPARE(document.revision(), quint64(0));
        QVERIFY(document.generation().isEmpty());
        QVERIFY(document.load(path));
        quint64 revision = document.revision();
        const QString generation = document.generation();
        QVERIFY(!QUuid(generation).isNull());
        QVERIFY(document.nodeById("") == nullptr);
        QVERIFY(document.nodeById("missing") == nullptr);
        auto* bar = document.nodeById("1");
        auto* a = document.nodeById("10");
        const auto rawBefore = snapshot(document);
        QVERIFY(document.rename(a, a->name()));
        QVERIFY(document.updateUrl(a, a->url()));
        QVERIFY(document.move(document.nodeById("15"), bar));
        QCOMPARE(document.revision(), revision);
        QCOMPARE(snapshot(document), rawBefore);
        QVERIFY(!document.isDirty());
        QVERIFY(document.rename(a, "renamed"));
        QCOMPARE(document.revision(), ++revision);
        QVERIFY(document.updateUrl(a, "https://changed.test/"));
        QCOMPARE(document.revision(), ++revision);
        auto* created = document.addFolder(bar, "new folder");
        QVERIFY(created);
        QCOMPARE(document.revision(), ++revision);
        auto* added = document.addBookmark(created, "new bookmark", "https://new.test/");
        QVERIFY(added);
        QCOMPARE(document.revision(), ++revision);
        QCOMPARE(document.nodeById(added->id()), added);
        BookmarkNode external(folder("999", QJsonArray{bookmark("1000")}), {}, nullptr);
        QVERIFY(document.add(&external, bar));
        QCOMPARE(document.revision(), ++revision);
        QVERIFY(bar->children.back()->id() != external.id());
        QVERIFY(bar->children.back()->guid() != external.guid());
        const QString removedId = created->id();
        const QString removedChildId = added->id();
        QVERIFY(document.remove(created));
        QCOMPARE(document.revision(), ++revision);
        QVERIFY(document.nodeById(removedId) == nullptr);
        QVERIFY(document.nodeById(removedChildId) == nullptr);
        a->addTag("direct edit");
        document.setDirty(true);
        QCOMPARE(document.revision(), ++revision);
        document.setDirty(true);
        QCOMPARE(document.revision(), ++revision);
        document.setDirty(false);
        QCOMPARE(document.revision(), revision);
        QCOMPARE(document.generation(), generation);
        QVERIFY(document.save({}, false));
        QCOMPARE(document.revision(), revision);
        QCOMPARE(document.generation(), generation);
        QVERIFY(document.load(path));
        QCOMPARE(document.revision(), revision + 1);
        QVERIFY(document.generation() != generation);
        QVERIFY(!document.isDirty());
    }

    void otherMutationsRejectForeignNodes()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("Bookmarks");
        QVERIFY(writeSample(path));
        BookmarkDocument document, foreign;
        QVERIFY(document.load(path));
        QVERIFY(foreign.load(path));
        const auto before = snapshot(document);
        const auto foreignBefore = snapshot(foreign);
        const auto revision = document.revision();
        QVERIFY(!document.addFolder(foreign.nodeById("1"), "bad"));
        QVERIFY(!document.addBookmark(foreign.nodeById("1"), "bad", "bad"));
        QVERIFY(!document.add(document.nodeById("10"), foreign.nodeById("1")));
        QVERIFY(!document.rename(foreign.nodeById("10"), "bad"));
        QVERIFY(!document.updateUrl(foreign.nodeById("10"), "bad"));
        QVERIFY(!document.remove(foreign.nodeById("10")));
        QVERIFY(!document.rename(document.nodeById("4"), "bad"));
        QVERIFY(!document.remove(document.nodeById("4")));
        QCOMPARE(snapshot(document), before);
        QCOMPARE(snapshot(foreign), foreignBefore);
        QCOMPARE(document.revision(), revision);
        QVERIFY(!document.isDirty());
    }

    void idsAreUnambiguousAndHandleLargeValues()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("Bookmarks");
        auto object = sample();
        auto roots = object["roots"].toObject();
        auto bar = roots["bookmark_bar"].toObject();
        auto children = bar["children"].toArray();
        children.append(bookmark("10"));
        children.append(bookmark(QString::number(std::numeric_limits<quint64>::max())));
        bar["children"] = children;
        roots["bookmark_bar"] = bar;
        object["roots"] = roots;
        QVERIFY(writeBytes(path, QJsonDocument(object).toJson()));
        BookmarkDocument document;
        QVERIFY(document.load(path));
        QVERIFY(document.nodeById("10") == nullptr);
        auto* added = document.addBookmark(document.nodeById("1"), "new", "https://example.test");
        QVERIFY(added);
        QCOMPARE(document.nodeById(added->id()), added);
        QCOMPARE(added->id(), QStringLiteral("5"));
    }

    void failedLoadPreservesDocumentAndBaseline()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("Bookmarks");
        const QString invalid = dir.filePath("invalid");
        QVERIFY(writeSample(path));
        BookmarkDocument document;
        QVERIFY(document.load(path));
        auto* original = document.nodeById("10");
        QVERIFY(document.rename(original, "unsaved"));
        const auto before = snapshot(document);
        const auto revision = document.revision();
        const auto generation = document.generation();
        const QList<QByteArray> invalidSamples = {"{", "[]", "{}", "{\"roots\":[]}",
            "{\"roots\":{\"bookmark_bar\":42}}",
            "{\"roots\":{\"bookmark_bar\":{\"children\":[42]}}}"};
        for (const auto& bytes : invalidSamples) {
            QVERIFY(writeBytes(invalid, bytes));
            QString error;
            QVERIFY(!document.load(invalid, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(snapshot(document), before);
            QCOMPARE(document.nodeById("10"), original);
            QCOMPARE(document.path(), path);
            QCOMPARE(document.revision(), revision);
            QCOMPARE(document.generation(), generation);
            QVERIFY(document.isDirty());
        }
        QVERIFY(!document.load(dir.filePath("missing")));
        QCOMPARE(document.nodeById("10"), original);
        QVERIFY(document.save({}, false));
        QCOMPARE(QJsonDocument::fromJson(readBytes(path)).object()["unknown_top"], sample()["unknown_top"]);
    }

    void jsonRoundTripPreservesUnknownFields()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("Bookmarks");
        const QString saved = dir.filePath("saved");
        QVERIFY(writeSample(path));
        BookmarkDocument document;
        QVERIFY(document.load(path));
        auto* leaf = document.nodeById("22");
        leaf->addTag("kept tag");
        document.setDirty(true);
        QVERIFY(document.moveNodes({document.nodeById("20")}, document.nodeById("1"), 1));
        const auto before = snapshot(document);
        const auto revision = document.revision();
        QVERIFY(document.save(saved, false));
        QCOMPARE(document.revision(), revision);
        QVERIFY(!document.isDirty());
        BookmarkDocument loaded;
        QVERIFY(loaded.load(saved));
        QCOMPARE(snapshot(loaded), before);
        QCOMPARE(loaded.nodeById("22")->tags, (QStringList{"kept tag"}));
        QCOMPARE(loaded.nodeById("22")->rootKey, QStringLiteral("bookmark_bar"));
        const auto output = QJsonDocument::fromJson(readBytes(saved)).object();
        QCOMPARE(output["unknown_top"], sample()["unknown_top"]);
        QVERIFY(!output.contains("checksum"));
        QVERIFY(output["roots"].toObject().contains("custom_root"));
        QCOMPARE(readBytes(path), QJsonDocument(sample()).toJson());
    }

    void externalChangeRefusesOverwrite_data()
    {
        QTest::addColumn<QString>("alias");
        QTest::newRow("default-path") << QStringLiteral("default");
        QTest::newRow("normalized-path") << QStringLiteral("normalized");
        QTest::newRow("relative-path") << QStringLiteral("relative");
        QTest::newRow("symlink") << QStringLiteral("symlink");
        QTest::newRow("directory-symlink") << QStringLiteral("directory-symlink");
        QTest::newRow("hardlink") << QStringLiteral("hardlink");
        QTest::newRow("missing-source") << QStringLiteral("missing");
    }

    void externalChangeRefusesOverwrite()
    {
        QFETCH(QString, alias);
        QTemporaryDir dir;
        const QString path = dir.filePath("Bookmarks");
        QVERIFY(writeSample(path));
        BookmarkDocument document;
        QVERIFY(document.load(path));
        QVERIFY(document.rename(document.nodeById("10"), "unsaved"));
        QString target;
        if (alias == "normalized") {
            QVERIFY(QDir().mkdir(dir.filePath("sub")));
            target = dir.filePath("sub/../Bookmarks");
        } else if (alias == "relative") {
            target = QDir::current().relativeFilePath(path);
        } else if (alias == "symlink" || alias == "directory-symlink" || alias == "hardlink") {
            const QString link = dir.filePath("alias");
            std::error_code error;
            if (alias == "hardlink") {
                std::filesystem::create_hard_link(nativePath(path), nativePath(link), error);
                target = link;
            } else if (alias == "directory-symlink") {
                std::filesystem::create_directory_symlink(nativePath(dir.path()), nativePath(link), error);
                target = QDir(link).filePath("Bookmarks");
            } else {
                std::filesystem::create_symlink(nativePath(path), nativePath(link), error);
                target = link;
            }
            if (error) QSKIP("Filesystem aliases are not available on this platform");
        }
        const QByteArray external = "external changes must survive";
        if (alias == "missing") QVERIFY(QFile::remove(path));
        else QVERIFY(writeBytes(path, external));
        const auto revision = document.revision();
        const auto generation = document.generation();
        QString error;
        QVERIFY(!document.save(target, false, &error));
        QVERIFY(error.contains(QStringLiteral("重新加载")));
        QVERIFY(error.contains(QStringLiteral("另存")));
        QCOMPARE(document.path(), path);
        QCOMPARE(document.revision(), revision);
        QCOMPARE(document.generation(), generation);
        QVERIFY(document.isDirty());
        if (alias != "missing") QCOMPARE(readBytes(path), external);
        else QVERIFY(!QFile::exists(path));
        QVERIFY(backups(path).isEmpty());
    }

    void uniqueBackupsAndSaveAsBaseline()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath("Bookmarks");
        const QString target = dir.filePath("save-as");
        QVERIFY(writeSample(source));
        BookmarkDocument document;
        QVERIFY(document.load(source));
        QByteArray previous = readBytes(source);
        QStringList oldBackups;
        for (int i = 0; i < 3; ++i) {
            QVERIFY(document.rename(document.nodeById("10"), QString::number(i)));
            const auto revision = document.revision();
            QVERIFY(document.save({}, false));
            const QStringList current = backups(source);
            QCOMPARE(current.size(), i + 1);
            QStringList created = current;
            for (const auto& old : oldBackups) created.removeAll(old);
            QCOMPARE(created.size(), 1);
            QCOMPARE(readBytes(dir.filePath(created.front())), previous);
            previous = readBytes(source);
            oldBackups = current;
            QCOMPARE(document.revision(), revision);
            QVERIFY(!document.isDirty());
        }
        QVERIFY(document.rename(document.nodeById("10"), "save as"));
        QVERIFY(writeBytes(source, "external source change"));
        QVERIFY(!document.save({}, false));
        QVERIFY(writeBytes(target, "previous save-as contents"));
        QVERIFY(document.save(target, false));
        QCOMPARE(document.path(), target);
        QCOMPARE(readBytes(source), QByteArray("external source change"));
        QCOMPARE(backups(target).size(), 1);
        QCOMPARE(readBytes(dir.filePath(backups(target).front())), QByteArray("previous save-as contents"));
        QVERIFY(document.rename(document.nodeById("10"), "second save as"));
        QVERIFY(document.save({}, false));
        QCOMPARE(backups(target).size(), 2);
        const QByteArray externalTarget = "changed after save as";
        QVERIFY(writeBytes(target, externalTarget));
        QVERIFY(document.rename(document.nodeById("10"), "must conflict"));
        QVERIFY(!document.save({}, false));
        QCOMPARE(readBytes(target), externalTarget);
        QCOMPARE(backups(target).size(), 2);
        QVERIFY(document.isDirty());
    }

    void saveThroughSymlinkKeepsAlias()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("Bookmarks");
        const QString alias = dir.filePath("alias");
        QVERIFY(writeSample(path));
        std::error_code error;
        std::filesystem::create_symlink(nativePath(path), nativePath(alias), error);
        if (error) QSKIP("Symbolic links are not available on this platform");
        BookmarkDocument document;
        QVERIFY(document.load(alias));
        QVERIFY(document.rename(document.nodeById("10"), "through alias"));
        QVERIFY(document.save({}, false));
        QVERIFY(QFileInfo(alias).isSymLink());
        QCOMPARE(readBytes(alias), readBytes(path));
        QCOMPARE(backups(path).size(), 1);
        QVERIFY(writeBytes(path, "external alias update"));
        QVERIFY(!document.save({}, false));
    }

    void savesStandaloneCopiesWhileChromeRuns()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("Bookmarks");
        QVERIFY(writeSample(path));
        BookmarkDocument document([] { return true; });
        QVERIFY(document.load(path));
        QVERIFY(document.rename(document.nodeById("10"), "saved with Chrome open"));
        QString error;
        QVERIFY2(document.save({}, true, &error), qPrintable(error));
        QVERIFY(!document.isDirty());
        BookmarkDocument reopened;
        QVERIFY(reopened.load(path));
        QCOMPARE(reopened.nodeById("10")->name(), QStringLiteral("saved with Chrome open"));
        QCOMPARE(backups(path).size(), 1);
        QVERIFY(document.rename(document.nodeById("10"), "second save"));
        QVERIFY2(document.save({}, true, &error), qPrintable(error));
        QCOMPARE(backups(path).size(), 2);
    }

    void runningChromeProtectsProfileButAllowsSaveAs()
    {
        QTemporaryDir directory;
        const QString profile = directory.filePath("User Data/Default");
        QVERIFY(QDir().mkpath(profile));
        QVERIFY(writeBytes(directory.filePath("User Data/Local State"), "{}"));
        QVERIFY(writeBytes(QDir(profile).filePath("Preferences"), "{}"));
        const QString source = QDir(profile).filePath("Bookmarks");
        const QString copy = directory.filePath("Saved Bookmarks.json");
        QVERIFY(writeSample(source));
        const auto original = readBytes(source);
        bool running = true;
        BookmarkDocument document([&running] { return running; });
        QVERIFY(document.load(source));
        QVERIFY(document.rename(document.nodeById("10"), "unsaved"));
        QString error;
        QVERIFY(!document.save({}, true, &error));
        QVERIFY(error.contains(QStringLiteral("另存为")));
        QVERIFY(document.isDirty());
        QCOMPARE(readBytes(source), original);
        QVERIFY(backups(source).isEmpty());
        QVERIFY2(document.save(copy, true, &error), qPrintable(error));
        QCOMPARE(document.path(), copy);
        QVERIFY(!document.isDirty());
        QCOMPARE(readBytes(source), original);
        QVERIFY(document.rename(document.nodeById("10"), "changed copy"));
        QVERIFY(!document.save(source, true, &error));
        QCOMPARE(document.path(), copy);
        QVERIFY(document.isDirty());
        running = false;
        QVERIFY2(document.save(source, true, &error), qPrintable(error));
        QCOMPARE(backups(source).size(), 1);
        BookmarkDocument reopened;
        QVERIFY(reopened.load(source));
        QCOMPARE(reopened.nodeById("10")->name(), QStringLiteral("changed copy"));
    }

    void runningChromeProtectsProfileAliases_data()
    {
        QTest::addColumn<bool>("hardLink");
        QTest::newRow("symlink") << false;
        QTest::newRow("hardlink") << true;
    }

    void runningChromeProtectsProfileAliases()
    {
        QFETCH(bool, hardLink);
        QTemporaryDir directory;
        const QString profile = directory.filePath("User Data/Default");
        QVERIFY(QDir().mkpath(profile));
        QVERIFY(writeBytes(directory.filePath("User Data/Local State"), "{}"));
        QVERIFY(writeBytes(QDir(profile).filePath("Preferences"), "{}"));
        const QString source = QDir(profile).filePath("Bookmarks");
        const QString alias = directory.filePath("alias.json");
        QVERIFY(writeSample(source));
        std::error_code linkError;
        if (hardLink) std::filesystem::create_hard_link(nativePath(source), nativePath(alias), linkError);
        else std::filesystem::create_symlink(nativePath(source), nativePath(alias), linkError);
        if (linkError) QSKIP("Filesystem aliases are not available on this platform");
        BookmarkDocument document([] { return true; });
        QVERIFY(document.load(source));
        QVERIFY(document.rename(document.nodeById("10"), "unsaved"));
        QString error;
        QVERIFY(!document.save(alias, true, &error));
        QVERIFY(error.contains(QStringLiteral("Chrome")));
        QCOMPARE(readBytes(source), QJsonDocument(sample()).toJson());
        QVERIFY(document.isDirty());
        QVERIFY(backups(source).isEmpty());
    }

    void chromeExitDoesNotBypassExternalChangeProtection()
    {
        QTemporaryDir directory;
        const QString source = directory.filePath("Bookmarks");
        QVERIFY(writeSample(source));
        BookmarkDocument document([] { return false; });
        QVERIFY(document.load(source));
        QVERIFY(document.rename(document.nodeById("10"), "local edit"));
        const QByteArray external = "changed by Chrome on exit";
        QVERIFY(writeBytes(source, external));
        QString error;
        QVERIFY(!document.save({}, true, &error));
        QCOMPARE(readBytes(source), external);
        QVERIFY(document.isDirty());
        QVERIFY2(document.save(directory.filePath("copy.json"), true, &error), qPrintable(error));
        QCOMPARE(readBytes(source), external);
    }

    void failedSavePreservesPathDirtyAndBaseline()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("Bookmarks");
        const QString blocker = dir.filePath("blocker");
        QVERIFY(writeSample(path));
        QVERIFY(writeBytes(blocker, "not a directory"));
        BookmarkDocument document;
        QVERIFY(document.load(path));
        QVERIFY(document.rename(document.nodeById("10"), "unsaved"));
        const auto revision = document.revision();
        const auto original = readBytes(path);
        const auto before = snapshot(document);
        QString error;
        QVERIFY(!document.save(blocker + "/Bookmarks", false, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(document.path(), path);
        QCOMPARE(document.revision(), revision);
        QCOMPARE(snapshot(document), before);
        QCOMPARE(readBytes(path), original);
        QVERIFY(document.isDirty());
        QVERIFY(document.save({}, false));
        QVERIFY(!document.isDirty());
    }

    void backupFailureRefusesOverwrite()
    {
#ifdef Q_OS_UNIX
        if (::geteuid() == 0) QSKIP("Permission failure requires an unprivileged user");
        QTemporaryDir dir;
        const QString path = dir.filePath("Bookmarks");
        QVERIFY(writeSample(path));
        BookmarkDocument document;
        QVERIFY(document.load(path));
        QVERIFY(document.rename(document.nodeById("10"), "unsaved"));
        const QByteArray original = readBytes(path);
        const auto revision = document.revision();
        const auto permissions = QFile::permissions(dir.path());
        QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        QString error;
        const bool saved = document.save({}, false, &error);
        const bool restored = QFile::setPermissions(dir.path(), permissions);
        QVERIFY(restored);
        QVERIFY(!saved);
        QVERIFY2(error.contains(QStringLiteral("备份")), qPrintable(error));
        QCOMPARE(readBytes(path), original);
        QVERIFY(backups(path).isEmpty());
        QVERIFY(document.isDirty());
        QCOMPARE(document.revision(), revision);
        QCOMPARE(document.path(), path);
        QVERIFY(document.save({}, false));
#else
        QSKIP("POSIX directory permissions are required to induce a backup-copy failure");
#endif
    }

    void writeOrCommitFailureKeepsOldBaseline()
    {
#ifdef Q_OS_UNIX
        // A file-size limit deterministically fails a large QSaveFile write (or its commit
        // flush) without a test hook, disk exhaustion, or touching any non-temporary file.
        QTemporaryDir dir;
        const QString path = dir.filePath("Bookmarks");
        const QString target = dir.filePath("too-large");
        QVERIFY(writeSample(path));
        BookmarkDocument document;
        QVERIFY(document.load(path));
        QVERIFY(document.rename(document.nodeById("10"), QString(128 * 1024, QLatin1Char('x'))));
        const auto revision = document.revision();
        const auto original = readBytes(path);
        struct rlimit previous {};
        if (::getrlimit(RLIMIT_FSIZE, &previous) != 0) QSKIP("Cannot read file size limit");
        auto limited = previous;
        limited.rlim_cur = 1024;
        const auto handler = std::signal(SIGXFSZ, SIG_IGN);
        if (handler == SIG_ERR) QSKIP("Cannot suppress file size limit signal");
        const bool limitedOk = ::setrlimit(RLIMIT_FSIZE, &limited) == 0;
        QString error;
        const bool saved = limitedOk && document.save(target, false, &error);
        const bool restored = ::setrlimit(RLIMIT_FSIZE, &previous) == 0;
        std::signal(SIGXFSZ, handler);
        QVERIFY(restored);
        if (!limitedOk) QSKIP("Cannot set file size limit");
        QVERIFY(!saved);
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(target));
        QCOMPARE(readBytes(path), original);
        QCOMPARE(document.path(), path);
        QCOMPARE(document.revision(), revision);
        QVERIFY(document.isDirty());
        QVERIFY(document.save({}, false));
        QVERIFY(!document.isDirty());
#else
        QSKIP("POSIX file size limits are required to induce a write/commit failure");
#endif
    }
};

QTEST_GUILESS_MAIN(BookmarkDocumentTest)
#include "BookmarkDocumentTest.moc"
