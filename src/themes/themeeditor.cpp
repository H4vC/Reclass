#include "themeeditor.h"
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QColorDialog>
#include <QLineEdit>
#include <QLabel>

namespace rcx {

ThemeEditor::ThemeEditor(const Theme& theme, QWidget* parent)
    : QDialog(parent), m_theme(theme)
{
    setWindowTitle("Edit Theme");
    setMinimumWidth(320);

    auto* form = new QFormLayout;

    // Name field
    auto* nameEdit = new QLineEdit(m_theme.name);
    connect(nameEdit, &QLineEdit::textChanged, this, [this](const QString& t) {
        m_theme.name = t;
    });
    form->addRow("Name", nameEdit);

    // Color swatches
    struct FieldDef { const char* label; QColor Theme::*ptr; };
    const FieldDef fields[] = {
        {"Background",      &Theme::background},
        {"Background Alt",  &Theme::backgroundAlt},
        {"Surface",         &Theme::surface},
        {"Border",          &Theme::border},
        {"Button",          &Theme::button},
        {"Text",            &Theme::text},
        {"Text Dim",        &Theme::textDim},
        {"Text Muted",      &Theme::textMuted},
        {"Text Faint",      &Theme::textFaint},
        {"Hover",           &Theme::hover},
        {"Selected",        &Theme::selected},
        {"Selection",       &Theme::selection},
        {"Keyword",         &Theme::syntaxKeyword},
        {"Number",          &Theme::syntaxNumber},
        {"String",          &Theme::syntaxString},
        {"Comment",         &Theme::syntaxComment},
        {"Preprocessor",    &Theme::syntaxPreproc},
        {"Type",            &Theme::syntaxType},
        {"Hover Span",      &Theme::indHoverSpan},
        {"Cmd Pill",        &Theme::indCmdPill},
        {"Data Changed",    &Theme::indDataChanged},
        {"Hint Green",      &Theme::indHintGreen},
        {"Pointer Marker",  &Theme::markerPtr},
        {"Cycle Marker",    &Theme::markerCycle},
        {"Error Marker",    &Theme::markerError},
    };

    for (const auto& f : fields) {
        auto* btn = new QPushButton;
        btn->setFixedSize(60, 24);
        btn->setCursor(Qt::PointingHandCursor);
        SwatchEntry entry{f.label, f.ptr, btn};
        m_swatches.append(entry);
        updateSwatch(m_swatches.last());

        int idx = m_swatches.size() - 1;
        connect(btn, &QPushButton::clicked, this, [this, idx]() {
            pickColor(m_swatches[idx]);
        });
        form->addRow(f.label, btn);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void ThemeEditor::updateSwatch(SwatchEntry& entry) {
    QColor c = m_theme.*entry.field;
    entry.button->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid #555; border-radius: 3px; }")
        .arg(c.name()));
    entry.button->setToolTip(c.name());
}

void ThemeEditor::pickColor(SwatchEntry& entry) {
    QColor c = QColorDialog::getColor(m_theme.*entry.field, this, entry.label);
    if (c.isValid()) {
        m_theme.*entry.field = c;
        updateSwatch(entry);
    }
}

} // namespace rcx
