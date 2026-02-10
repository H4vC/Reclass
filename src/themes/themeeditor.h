#pragma once
#include "theme.h"
#include <QDialog>
#include <QVector>
#include <QPushButton>

namespace rcx {

class ThemeEditor : public QDialog {
    Q_OBJECT
public:
    explicit ThemeEditor(const Theme& theme, QWidget* parent = nullptr);
    Theme result() const { return m_theme; }

private:
    Theme m_theme;

    struct SwatchEntry {
        const char* label;
        QColor Theme::*field;
        QPushButton* button;
    };
    QVector<SwatchEntry> m_swatches;

    void updateSwatch(SwatchEntry& entry);
    void pickColor(SwatchEntry& entry);
};

} // namespace rcx
