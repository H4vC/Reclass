#pragma once
#include "core.h"
#include <QStandardItemModel>
#include <QStandardItem>
#include <algorithm>

namespace rcx {

// Recursively add children of parentId as tree items under parentItem.
inline void addWorkspaceChildren(QStandardItem* parentItem,
                                 const NodeTree& tree,
                                 uint64_t parentId,
                                 void* subPtr) {
    QVector<int> children = tree.childrenOf(parentId);
    std::sort(children.begin(), children.end(), [&](int a, int b) {
        return tree.nodes[a].offset < tree.nodes[b].offset;
    });

    for (int idx : children) {
        const Node& node = tree.nodes[idx];

        // Skip hex preview nodes — they are padding/filler, not meaningful fields
        if (isHexNode(node.kind)) continue;

        QString display;
        if (node.kind == NodeKind::Struct) {
            QString typeName = node.structTypeName.isEmpty()
                ? node.name : node.structTypeName;
            display = QStringLiteral("%1 (%2)")
                .arg(typeName, node.resolvedClassKeyword());
        } else {
            display = QStringLiteral("%1 (%2)")
                .arg(node.name, QString::fromLatin1(kindToString(node.kind)));
        }

        auto* item = new QStandardItem(display);
        item->setData(QVariant::fromValue(subPtr), Qt::UserRole);
        if (node.kind == NodeKind::Struct)
            item->setData(QVariant::fromValue(node.id), Qt::UserRole + 1);
        item->setData(QVariant::fromValue(node.id), Qt::UserRole + 2);  // nodeId for scroll

        if (node.kind == NodeKind::Struct)
            addWorkspaceChildren(item, tree, node.id, subPtr);

        parentItem->appendRow(item);
    }
}

inline void buildWorkspaceModel(QStandardItemModel* model,
                                const NodeTree& tree,
                                const QString& projectName,
                                void* subPtr = nullptr) {
    model->clear();
    model->setHorizontalHeaderLabels({QStringLiteral("Name")});

    auto* projectItem = new QStandardItem(projectName);
    projectItem->setData(QVariant::fromValue(subPtr), Qt::UserRole);

    addWorkspaceChildren(projectItem, tree, 0, subPtr);

    model->appendRow(projectItem);
}

} // namespace rcx
