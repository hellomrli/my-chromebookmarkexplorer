#pragma once

#include <QString>
#include <QVector>

#include <memory>
#include <vector>

class BookmarkNode;
class QTextStream;

class ImportExport {
public:
    // 导出
    static bool exportToHtml(const std::vector<std::unique_ptr<BookmarkNode>>& roots, const QString& filePath, QString* error);
    static bool exportToJson(const std::vector<std::unique_ptr<BookmarkNode>>& roots, const QString& filePath, QString* error);
    static bool exportToCsv(const std::vector<std::unique_ptr<BookmarkNode>>& roots, const QString& filePath, QString* error);

    // 导入
    static bool importFromHtml(const QString& filePath, QVector<BookmarkNode*>& roots, QString* error);
    static bool importFromJson(const QString& filePath, QVector<BookmarkNode*>& roots, QString* error);

private:
    static void writeHtmlNode(QTextStream& stream, BookmarkNode* node, int level);
    static void collectFlatNodes(BookmarkNode* node, QVector<BookmarkNode*>& nodes, const QString& parentPath);
};
