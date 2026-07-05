#include "BatchEditDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QVBoxLayout>

BatchEditDialog::BatchEditDialog(int count, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("批量编辑网址"));
    resize(600, 400);

    auto* layout = new QVBoxLayout(this);

    auto* infoLabel = new QLabel(QStringLiteral("将对 %1 个书签的网址进行批量替换").arg(count), this);
    layout->addWidget(infoLabel);

    auto* formLayout = new QFormLayout();

    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText(QStringLiteral("例如: http://old-domain.com"));
    formLayout->addRow(QStringLiteral("查找内容:"), searchEdit_);

    replaceEdit_ = new QLineEdit(this);
    replaceEdit_->setPlaceholderText(QStringLiteral("例如: https://new-domain.com"));
    formLayout->addRow(QStringLiteral("替换为:"), replaceEdit_);

    layout->addLayout(formLayout);

    regexCheck_ = new QCheckBox(QStringLiteral("使用正则表达式"), this);
    regexCheck_->setToolTip(QStringLiteral("启用后可使用正则表达式进行复杂匹配，例如: ^http://(.+)$ → https://\\1"));
    layout->addWidget(regexCheck_);

    caseCheck_ = new QCheckBox(QStringLiteral("区分大小写"), this);
    layout->addWidget(caseCheck_);

    previewLabel_ = new QLabel(QStringLiteral("替换预览:"), this);
    layout->addWidget(previewLabel_);

    previewText_ = new QTextEdit(this);
    previewText_->setReadOnly(true);
    previewText_->setMaximumHeight(150);
    previewText_->setPlaceholderText(QStringLiteral("输入查找和替换内容后，这里会显示前几个匹配项的预览"));
    layout->addWidget(previewText_);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);
}

QString BatchEditDialog::searchPattern() const
{
    return searchEdit_->text();
}

QString BatchEditDialog::replacePattern() const
{
    return replaceEdit_->text();
}

bool BatchEditDialog::useRegex() const
{
    return regexCheck_->isChecked();
}

bool BatchEditDialog::caseSensitive() const
{
    return caseCheck_->isChecked();
}
