#pragma once

#include <QDialog>

class QLineEdit;
class QListWidget;

class TagDialog : public QDialog {
    Q_OBJECT

public:
    explicit TagDialog(const QStringList& currentTags, QWidget* parent = nullptr);

    QStringList tags() const;

private slots:
    void addTag();
    void removeTag();

private:
    QLineEdit* tagInput_ = nullptr;
    QListWidget* tagList_ = nullptr;
};
