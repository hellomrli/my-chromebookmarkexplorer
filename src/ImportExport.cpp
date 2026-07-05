#include "ImportExport.h"
#include "BookmarkNode.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

bool ImportExport::exportToHtml(const QVector<BookmarkNode*>& roots, const QString& filePath, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("无法打开文件: %1").arg(file.errorString());
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    stream << "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n";
    stream << "<!-- This is an automatically generated file.\n";
    stream << "     It will be read and overwritten.\n";
    stream << "     DO NOT EDIT! -->\n";
    stream << "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n";
    stream << "<TITLE>Bookmarks</TITLE>\n";
    stream << "<H1>Bookmarks</H1>\n";
    stream << "<DL><p>\n";

    for (auto* root : roots) {
        writeHtmlNode(stream, root, 1);
    }

    stream << "</DL><p>\n";
    return true;
}

void ImportExport::writeHtmlNode(QTextStream& stream, BookmarkNode* node, int level)
{
    const QString indent = QString("    ").repeated(level);

    if (node->isFolder()) {
        stream << indent << "<DT><H3>" << node->name().toHtmlEscaped() << "</H3>\n";
        stream << indent << "<DL><p>\n";
        for (const auto& child : node->children) {
            writeHtmlNode(stream, child.get(), level + 1);
        }
        stream << indent << "</DL><p>\n";
    } else if (node->isUrl()) {
        stream << indent << "<DT><A HREF=\"" << node->url().toHtmlEscaped() << "\"";
        if (!node->dateAdded().isEmpty()) {
            stream << " ADD_DATE=\"" << node->dateAdded() << "\"";
        }
        stream << ">" << node->name().toHtmlEscaped() << "</A>\n";
    }
}

bool ImportExport::exportToJson(const QVector<BookmarkNode*>& roots, const QString& filePath, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("无法打开文件: %1").arg(file.errorString());
        return false;
    }

    QJsonObject root;
    root["version"] = 1;

    QJsonObject rootsObj;
    for (auto* node : roots) {
        rootsObj[node->name()] = node->toJson();
    }
    root["roots"] = rootsObj;

    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

bool ImportExport::exportToCsv(const QVector<BookmarkNode*>& roots, const QString& filePath, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("无法打开文件: %1").arg(file.errorString());
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    // CSV 头
    stream << "\"Name\",\"URL\",\"Path\",\"Date Added\"\n";

    // 收集所有书签节点
    QVector<BookmarkNode*> allNodes;
    for (auto* root : roots) {
        collectFlatNodes(root, allNodes, root->name());
    }

    // 写入 CSV 行
    for (auto* node : allNodes) {
        if (node->isUrl()) {
            stream << "\"" << node->name().replace("\"", "\"\"") << "\",";
            stream << "\"" << node->url().replace("\"", "\"\"") << "\",";
            stream << "\"" << node->path().replace("\"", "\"\"") << "\",";
            stream << "\"" << node->formattedDateAdded().replace("\"", "\"\"") << "\"\n";
        }
    }

    return true;
}

void ImportExport::collectFlatNodes(BookmarkNode* node, QVector<BookmarkNode*>& nodes, const QString& parentPath)
{
    if (node->isUrl()) {
        nodes.push_back(node);
    }

    for (const auto& child : node->children) {
        collectFlatNodes(child.get(), nodes, parentPath + "/" + node->name());
    }
}

bool ImportExport::importFromHtml(const QString& filePath, QVector<BookmarkNode*>& roots, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("无法打开文件: %1").arg(file.errorString());
        return false;
    }

    // 简化的 HTML 导入实现
    // 完整实现需要 HTML 解析器
    if (error) *error = QStringLiteral("HTML 导入功能尚未完全实现");
    return false;
}

bool ImportExport::importFromJson(const QString& filePath, QVector<BookmarkNode*>& roots, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("无法打开文件: %1").arg(file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) *error = QStringLiteral("JSON 解析失败: %1").arg(parseError.errorString());
        return false;
    }

    const QJsonObject rootObj = doc.object();
    const QJsonObject rootsObj = rootObj["roots"].toObject();

    for (const QString& key : rootsObj.keys()) {
        auto* node = BookmarkNode::fromJson(rootsObj[key].toObject());
        if (node) {
            roots.push_back(node);
        }
    }

    return true;
}
