#pragma once

#include <QDialog>

class QLineEdit;
class QTextEdit;
class QCheckBox;
class QLabel;

class BatchEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit BatchEditDialog(int count, QWidget* parent = nullptr);

    QString searchPattern() const;
    QString replacePattern() const;
    bool useRegex() const;
    bool caseSensitive() const;

private:
    QLineEdit* searchEdit_ = nullptr;
    QLineEdit* replaceEdit_ = nullptr;
    QCheckBox* regexCheck_ = nullptr;
    QCheckBox* caseCheck_ = nullptr;
    QLabel* previewLabel_ = nullptr;
    QTextEdit* previewText_ = nullptr;
};
