#include "TagDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

TagDialog::TagDialog(const QStringList& currentTags, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("管理标签"));
    resize(400, 300);

    auto* layout = new QVBoxLayout(this);

    auto* infoLabel = new QLabel(QStringLiteral("为书签添加标签，方便分类和筛选"), this);
    layout->addWidget(infoLabel);

    auto* inputLayout = new QHBoxLayout();
    tagInput_ = new QLineEdit(this);
    tagInput_->setPlaceholderText(QStringLiteral("输入标签名称"));
    inputLayout->addWidget(tagInput_);

    auto* addButton = new QPushButton(QStringLiteral("添加"), this);
    connect(addButton, &QPushButton::clicked, this, &TagDialog::addTag);
    inputLayout->addWidget(addButton);
    layout->addLayout(inputLayout);

    auto* listLabel = new QLabel(QStringLiteral("当前标签:"), this);
    layout->addWidget(listLabel);

    tagList_ = new QListWidget(this);
    tagList_->addItems(currentTags);
    layout->addWidget(tagList_);

    auto* removeButton = new QPushButton(QStringLiteral("删除选中"), this);
    connect(removeButton, &QPushButton::clicked, this, &TagDialog::removeTag);
    layout->addWidget(removeButton);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    connect(tagInput_, &QLineEdit::returnPressed, this, &TagDialog::addTag);
}

QStringList TagDialog::tags() const
{
    QStringList result;
    for (int i = 0; i < tagList_->count(); ++i) {
        result.append(tagList_->item(i)->text());
    }
    return result;
}

void TagDialog::addTag()
{
    const QString tag = tagInput_->text().trimmed();
    if (tag.isEmpty()) {
        return;
    }

    // 检查是否已存在
    for (int i = 0; i < tagList_->count(); ++i) {
        if (tagList_->item(i)->text() == tag) {
            tagInput_->clear();
            return;
        }
    }

    tagList_->addItem(tag);
    tagInput_->clear();
}

void TagDialog::removeTag()
{
    auto items = tagList_->selectedItems();
    for (auto* item : items) {
        delete tagList_->takeItem(tagList_->row(item));
    }
}
