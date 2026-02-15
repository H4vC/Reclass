#include "optionsdialog.h"
#include "themes/thememanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QGroupBox>
#include <QLabel>
#include <QTreeWidgetItem>
#include <QGraphicsDropShadowEffect>
#include <QEvent>
#include <functional>

namespace rcx {

OptionsDialog::OptionsDialog(const OptionsResult& current, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Options");
    setFixedSize(700, 450);

    const auto& t = ThemeManager::instance().current();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // -- Middle: left column (search + tree) | right column (pages) --
    auto* middleLayout = new QHBoxLayout;
    middleLayout->setSpacing(8);

    // Left column: search bar + tree
    auto* leftColumn = new QVBoxLayout;
    leftColumn->setSpacing(4);

    m_search = new QLineEdit;
    m_search->setPlaceholderText("Search Options (Ctrl+E)");
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, &OptionsDialog::filterTree);
    leftColumn->addWidget(m_search);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setFixedWidth(200);

    auto* envItem = new QTreeWidgetItem(m_tree, {"Environment"});
    auto* generalItem = new QTreeWidgetItem(envItem, {"General"});
    m_tree->expandAll();
    m_tree->setCurrentItem(generalItem);
    leftColumn->addWidget(m_tree, 1);

    middleLayout->addLayout(leftColumn);

    // Right column: stacked pages with group boxes
    m_pages = new QStackedWidget;

    // -- General page --
    auto* generalPage = new QWidget;
    auto* generalLayout = new QVBoxLayout(generalPage);
    generalLayout->setContentsMargins(0, 0, 0, 0);
    generalLayout->setSpacing(8);

    // Visual Experience group box
    auto* visualGroup = new QGroupBox("Visual Experience");
    auto* visualLayout = new QFormLayout(visualGroup);
    visualLayout->setSpacing(8);
    visualLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_themeCombo = new QComboBox;
    auto& tm = ThemeManager::instance();
    for (const auto& theme : tm.themes())
        m_themeCombo->addItem(theme.name);
    m_themeCombo->setCurrentIndex(current.themeIndex);
    m_themeCombo->setObjectName("themeCombo");
    visualLayout->addRow("Color theme:", m_themeCombo);

    m_fontCombo = new QComboBox;
    m_fontCombo->addItem("JetBrains Mono");
    m_fontCombo->addItem("Consolas");
    m_fontCombo->setCurrentText(current.fontName);
    m_fontCombo->setObjectName("fontCombo");
    visualLayout->addRow("Editor Font:", m_fontCombo);

    m_titleCaseCheck = new QCheckBox("Apply title case styling to menu bar");
    m_titleCaseCheck->setChecked(current.menuBarTitleCase);
    visualLayout->addRow(m_titleCaseCheck);

    generalLayout->addWidget(visualGroup);

    // Safe Mode group box
    auto* safeModeGroup = new QGroupBox("Preview Features");
    auto* safeModeLayout = new QVBoxLayout(safeModeGroup);
    safeModeLayout->setSpacing(4);

    m_safeModeCheck = new QCheckBox("Safe Mode");
    m_safeModeCheck->setChecked(current.safeMode);
    m_safeModeCheck->setStyleSheet(QStringLiteral(
        "QCheckBox { font-weight: bold; }"));
    safeModeLayout->addWidget(m_safeModeCheck);

    auto* safeModeDesc = new QLabel(
        "Enable to use the default OS icon for this application and "
        "create the window with the name of the executable file.");
    safeModeDesc->setWordWrap(true);
    safeModeDesc->setContentsMargins(20, 0, 0, 0);  // indent under checkbox
    safeModeLayout->addWidget(safeModeDesc);

    generalLayout->addWidget(safeModeGroup);
    generalLayout->addStretch();

    m_pages->addWidget(generalPage);                     // index 0
    m_pageKeywords[generalItem] = collectPageKeywords(generalPage);

    // -- AI Features page --
    auto* aiItem = new QTreeWidgetItem(envItem, {"AI Features"});

    auto* aiPage = new QWidget;
    auto* aiLayout = new QVBoxLayout(aiPage);
    aiLayout->setContentsMargins(0, 0, 0, 0);
    aiLayout->setSpacing(8);

    auto* mcpGroup = new QGroupBox("MCP Server");
    auto* mcpLayout = new QVBoxLayout(mcpGroup);
    mcpLayout->setSpacing(4);

    m_autoMcpCheck = new QCheckBox("Auto-start MCP server");
    m_autoMcpCheck->setChecked(current.autoStartMcp);
    m_autoMcpCheck->setStyleSheet(QStringLiteral(
        "QCheckBox { font-weight: bold; }"));
    mcpLayout->addWidget(m_autoMcpCheck);

    auto* mcpDesc = new QLabel(
        "Automatically start the MCP bridge server when the application launches, "
        "allowing external AI tools to connect and interact with the editor.");
    mcpDesc->setWordWrap(true);
    mcpDesc->setContentsMargins(20, 0, 0, 0);
    mcpLayout->addWidget(mcpDesc);

    aiLayout->addWidget(mcpGroup);
    aiLayout->addStretch();

    m_pages->addWidget(aiPage);                          // index 1
    m_pageKeywords[aiItem] = collectPageKeywords(aiPage);

    middleLayout->addWidget(m_pages, 1);

    mainLayout->addLayout(middleLayout, 1);

    // Tree <-> page connection
    m_itemPageIndex[generalItem] = 0;
    m_itemPageIndex[aiItem] = 1;
    connect(m_tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* item, QTreeWidgetItem*) {
        if (!item) return;
        auto it = m_itemPageIndex.find(item);
        if (it != m_itemPageIndex.end())
            m_pages->setCurrentIndex(it.value());
    });

    // -- Button box --
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    // -- Styling --

    // Combo boxes: set directly so the popup (top-level widget) inherits it
    QString comboStyle = QStringLiteral(
        "QComboBox {"
        "  background: %1; color: %2; border: 1px solid %3;"
        "  padding: 3px 8px; font-size: 12px;"
        "}"
        "QComboBox::drop-down {"
        "  border: none; border-left: 1px solid %3;"
        "  width: 20px;"
        "}"
        "QComboBox::down-arrow {"
        "  image: url(:/vsicons/chevron-down.svg);"
        "  width: 12px; height: 12px;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background: %1; color: %2; border: 1px solid %3;"
        "  selection-background-color: %4;"
        "}")
        .arg(t.backgroundAlt.name(), t.text.name(),
             t.border.name(), t.hover.name());
    m_themeCombo->setStyleSheet(comboStyle);
    m_fontCombo->setStyleSheet(comboStyle);

    // Dialog-wide stylesheet for everything else
    setStyleSheet(QStringLiteral(
        "QDialog { background: %1; }"

        "QLineEdit {"
        "  background: %2; color: %3; border: 1px solid %4;"
        "  padding: 4px 8px; font-size: 12px;"
        "}"

        "QTreeWidget {"
        "  background: %2; color: %3; border: 1px solid %4;"
        "  font-size: 12px; outline: none;"
        "}"
        "QTreeWidget::item { padding: 3px 0; outline: none; }"
        "QTreeWidget::item:selected { background: %5; color: %3; }"
        "QTreeWidget::item:hover { background: %6; }"

        "QGroupBox {"
        "  color: %3; border: 1px solid %4;"
        "  margin-top: 8px; padding: 12px 8px 8px 8px;"
        "  font-size: 12px; font-weight: bold;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 8px; padding: 0 4px;"
        "}"

        "QLabel { color: %3; font-size: 12px; }"

        "QCheckBox { color: %3; font-size: 12px; spacing: 6px; }"

        "QPushButton {"
        "  background: %2; color: %3; border: 1px solid %4;"
        "  padding: 5px 16px; min-width: 70px; font-size: 12px;"
        "  outline: none;"
        "}"
        "QPushButton:hover { background: %6; }"
        "QPushButton:pressed { background: %1; }"
        "QPushButton:focus { outline: none; }")
        .arg(t.background.name(),    // %1
             t.backgroundAlt.name(),  // %2
             t.text.name(),           // %3
             t.border.name(),         // %4
             t.selection.name(),      // %5
             t.hover.name()));        // %6

    // Install hover shadow on interactive widgets (not buttons — they use stylesheet hover)
    for (auto* w : {static_cast<QWidget*>(m_search),
                    static_cast<QWidget*>(m_themeCombo),
                    static_cast<QWidget*>(m_fontCombo),
                    static_cast<QWidget*>(m_titleCaseCheck),
                    static_cast<QWidget*>(m_safeModeCheck),
                    static_cast<QWidget*>(m_autoMcpCheck)})
        w->installEventFilter(this);

    m_shadowColor = t.text;
    m_shadowColor.setAlpha(80);
}

bool OptionsDialog::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Enter) {
        auto* w = qobject_cast<QWidget*>(obj);
        if (w && !w->graphicsEffect()) {
            auto* shadow = new QGraphicsDropShadowEffect(w);
            shadow->setBlurRadius(12);
            shadow->setOffset(0, 0);
            shadow->setColor(m_shadowColor);
            w->setGraphicsEffect(shadow);
        }
    } else if (event->type() == QEvent::Leave) {
        auto* w = qobject_cast<QWidget*>(obj);
        if (w)
            w->setGraphicsEffect(nullptr);
    }
    return QDialog::eventFilter(obj, event);
}

OptionsResult OptionsDialog::result() const {
    OptionsResult r;
    r.themeIndex = m_themeCombo->currentIndex();
    r.fontName = m_fontCombo->currentText();
    r.menuBarTitleCase = m_titleCaseCheck->isChecked();
    r.safeMode = m_safeModeCheck->isChecked();
    r.autoStartMcp = m_autoMcpCheck->isChecked();
    return r;
}

QStringList OptionsDialog::collectPageKeywords(QWidget* page) {
    QStringList keywords;
    for (auto* child : page->findChildren<QWidget*>()) {
        if (auto* label = qobject_cast<QLabel*>(child))
            keywords << label->text();
        else if (auto* cb = qobject_cast<QCheckBox*>(child))
            keywords << cb->text();
        else if (auto* gb = qobject_cast<QGroupBox*>(child))
            keywords << gb->title();
        else if (auto* combo = qobject_cast<QComboBox*>(child)) {
            for (int i = 0; i < combo->count(); ++i)
                keywords << combo->itemText(i);
        }
    }
    return keywords;
}

void OptionsDialog::filterTree(const QString& text) {
    std::function<bool(QTreeWidgetItem*)> filter = [&](QTreeWidgetItem* item) -> bool {
        bool anyChildVisible = false;
        for (int i = 0; i < item->childCount(); ++i) {
            if (filter(item->child(i)))
                anyChildVisible = true;
        }

        bool selfMatch = item->text(0).contains(text, Qt::CaseInsensitive);
        if (!selfMatch) {
            for (const auto& kw : m_pageKeywords.value(item)) {
                if (kw.contains(text, Qt::CaseInsensitive)) {
                    selfMatch = true;
                    break;
                }
            }
        }
        bool visible = selfMatch || anyChildVisible;
        item->setHidden(!visible);

        if (visible && item->childCount() > 0)
            item->setExpanded(true);

        return visible;
    };

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        filter(m_tree->topLevelItem(i));
}

} // namespace rcx
