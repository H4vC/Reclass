#pragma once
#include "themes/theme.h"
#include <QWidget>
#include <QMenuBar>
#include <QToolButton>
#include <QLabel>
#include <QHBoxLayout>

namespace rcx {

class TitleBarWidget : public QWidget {
    Q_OBJECT
public:
    explicit TitleBarWidget(QWidget* parent = nullptr);

    QMenuBar* menuBar() const { return m_menuBar; }
    void applyTheme(const Theme& theme);
    void setShowIcon(bool show);

    void updateMaximizeIcon();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel*      m_appLabel   = nullptr;
    QMenuBar*    m_menuBar    = nullptr;
    QToolButton* m_btnMin     = nullptr;
    QToolButton* m_btnMax     = nullptr;
    QToolButton* m_btnClose   = nullptr;

    Theme m_theme;

    QToolButton* makeChromeButton(const QString& iconPath);
    void toggleMaximize();
};

} // namespace rcx
