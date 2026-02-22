#include <QtTest/QtTest>
#include "core.h"
#include "imports/import_pdb.h"

using namespace rcx;

class TestImportPdb : public QObject {
    Q_OBJECT
private slots:
    void missingFileReturnsError();
    void importKProcess();
    void verifyDispatcherHeader();
    void verifyListEntry();
    void importFilteredStruct();
    void enumerateTypes();
    void importSelected();
};

static const QString kPdbPath = QStringLiteral(
    "C:/Symbols/ntkrnlmp.pdb/0762CF42EF7F3E8116EF7329ADAA09A31/ntkrnlmp.pdb");

// Find a root struct by structTypeName
static int findRootStruct(const NodeTree& tree, const QString& name) {
    for (int i = 0; i < tree.nodes.size(); i++) {
        if (tree.nodes[i].parentId == 0 &&
            tree.nodes[i].kind == NodeKind::Struct &&
            tree.nodes[i].structTypeName == name)
            return i;
    }
    return -1;
}

// Find a child of parentId by name
static int findChildNode(const NodeTree& tree, uint64_t parentId, const QString& name) {
    for (int i = 0; i < tree.nodes.size(); i++) {
        if (tree.nodes[i].parentId == parentId && tree.nodes[i].name == name)
            return i;
    }
    return -1;
}

void TestImportPdb::missingFileReturnsError() {
    QString err;
    NodeTree tree = importPdb(QStringLiteral("C:/nonexistent.pdb"), {}, &err);
    QVERIFY(tree.nodes.isEmpty());
    QVERIFY(!err.isEmpty());
}

void TestImportPdb::importKProcess() {
    if (!QFile::exists(kPdbPath))
        QSKIP("ntkrnlmp.pdb not found at expected path");

    QString err;
    NodeTree tree = importPdb(kPdbPath, QStringLiteral("_KPROCESS"), &err);
    QVERIFY2(!tree.nodes.isEmpty(), qPrintable(err));

    // Find _KPROCESS root struct
    int kpIdx = findRootStruct(tree, QStringLiteral("_KPROCESS"));
    QVERIFY2(kpIdx >= 0, "Expected _KPROCESS root struct");
    uint64_t kpId = tree.nodes[kpIdx].id;

    // Verify Header field at offset 0 → embedded _DISPATCHER_HEADER
    int headerIdx = findChildNode(tree, kpId, QStringLiteral("Header"));
    QVERIFY2(headerIdx >= 0, "Expected 'Header' child of _KPROCESS");
    QCOMPARE(tree.nodes[headerIdx].kind, NodeKind::Struct);
    QCOMPARE(tree.nodes[headerIdx].structTypeName, QStringLiteral("_DISPATCHER_HEADER"));
    QCOMPARE(tree.nodes[headerIdx].offset, 0);

    // Verify ProfileListHead at offset 0x18 → embedded _LIST_ENTRY
    int profileIdx = findChildNode(tree, kpId, QStringLiteral("ProfileListHead"));
    QVERIFY2(profileIdx >= 0, "Expected 'ProfileListHead' child of _KPROCESS");
    QCOMPARE(tree.nodes[profileIdx].kind, NodeKind::Struct);
    QCOMPARE(tree.nodes[profileIdx].structTypeName, QStringLiteral("_LIST_ENTRY"));
    QCOMPARE(tree.nodes[profileIdx].offset, 0x18);
}

void TestImportPdb::verifyDispatcherHeader() {
    if (!QFile::exists(kPdbPath))
        QSKIP("ntkrnlmp.pdb not found at expected path");

    QString err;
    NodeTree tree = importPdb(kPdbPath, QStringLiteral("_KPROCESS"), &err);
    QVERIFY2(!tree.nodes.isEmpty(), qPrintable(err));

    // _DISPATCHER_HEADER should be imported as a transitive dependency
    int dhIdx = findRootStruct(tree, QStringLiteral("_DISPATCHER_HEADER"));
    QVERIFY2(dhIdx >= 0, "_DISPATCHER_HEADER should be imported as a dependency");

    uint64_t dhId = tree.nodes[dhIdx].id;
    auto kids = tree.childrenOf(dhId);
    QVERIFY2(!kids.isEmpty(), "_DISPATCHER_HEADER should have children (fields)");

    // Look for WaitListHead — a _LIST_ENTRY at offset 0x10 in most builds
    int waitIdx = findChildNode(tree, dhId, QStringLiteral("WaitListHead"));
    QVERIFY2(waitIdx >= 0, "Expected 'WaitListHead' in _DISPATCHER_HEADER");
    QCOMPARE(tree.nodes[waitIdx].kind, NodeKind::Struct);
    QCOMPARE(tree.nodes[waitIdx].structTypeName, QStringLiteral("_LIST_ENTRY"));
}

void TestImportPdb::verifyListEntry() {
    if (!QFile::exists(kPdbPath))
        QSKIP("ntkrnlmp.pdb not found at expected path");

    QString err;
    NodeTree tree = importPdb(kPdbPath, QStringLiteral("_KPROCESS"), &err);
    QVERIFY2(!tree.nodes.isEmpty(), qPrintable(err));

    // _LIST_ENTRY should be imported (used by ProfileListHead and others)
    int leIdx = findRootStruct(tree, QStringLiteral("_LIST_ENTRY"));
    QVERIFY2(leIdx >= 0, "_LIST_ENTRY should be imported");

    uint64_t leId = tree.nodes[leIdx].id;

    // Flink at offset 0 — pointer to _LIST_ENTRY
    int flinkIdx = findChildNode(tree, leId, QStringLiteral("Flink"));
    QVERIFY2(flinkIdx >= 0, "Expected 'Flink' in _LIST_ENTRY");
    QCOMPARE(tree.nodes[flinkIdx].kind, NodeKind::Pointer64);
    QCOMPARE(tree.nodes[flinkIdx].offset, 0);

    // Blink at offset 8 — pointer to _LIST_ENTRY
    int blinkIdx = findChildNode(tree, leId, QStringLiteral("Blink"));
    QVERIFY2(blinkIdx >= 0, "Expected 'Blink' in _LIST_ENTRY");
    QCOMPARE(tree.nodes[blinkIdx].kind, NodeKind::Pointer64);
    QCOMPARE(tree.nodes[blinkIdx].offset, 8);

    // Both should point back to _LIST_ENTRY (self-referencing)
    QCOMPARE(tree.nodes[flinkIdx].refId, leId);
    QCOMPARE(tree.nodes[blinkIdx].refId, leId);
}

void TestImportPdb::importFilteredStruct() {
    if (!QFile::exists(kPdbPath))
        QSKIP("ntkrnlmp.pdb not found at expected path");

    QString err;
    NodeTree tree = importPdb(kPdbPath, QStringLiteral("_LIST_ENTRY"), &err);
    QVERIFY2(!tree.nodes.isEmpty(), qPrintable(err));

    int leIdx = findRootStruct(tree, QStringLiteral("_LIST_ENTRY"));
    QVERIFY(leIdx >= 0);

    // _LIST_ENTRY only references itself, so exactly 1 root struct
    int rootCount = 0;
    for (const auto& n : tree.nodes)
        if (n.parentId == 0 && n.kind == NodeKind::Struct) rootCount++;
    QCOMPARE(rootCount, 1);
}

void TestImportPdb::enumerateTypes() {
    if (!QFile::exists(kPdbPath))
        QSKIP("ntkrnlmp.pdb not found at expected path");

    QString err;
    QVector<PdbTypeInfo> types = enumeratePdbTypes(kPdbPath, &err);
    QVERIFY2(!types.isEmpty(), qPrintable(err));

    // Should have hundreds of types in ntkrnlmp
    QVERIFY2(types.size() > 100,
             qPrintable(QStringLiteral("Expected >100 types, got %1").arg(types.size())));

    // Verify _KPROCESS is present
    bool foundKProcess = false;
    bool foundListEntry = false;
    for (const auto& t : types) {
        if (t.name == QStringLiteral("_KPROCESS")) {
            foundKProcess = true;
            QVERIFY2(t.childCount > 0, "_KPROCESS should have children");
            QVERIFY2(t.size > 0, "_KPROCESS should have non-zero size");
        }
        if (t.name == QStringLiteral("_LIST_ENTRY")) {
            foundListEntry = true;
        }
    }
    QVERIFY2(foundKProcess, "_KPROCESS not found in enumerated types");
    QVERIFY2(foundListEntry, "_LIST_ENTRY not found in enumerated types");
}

void TestImportPdb::importSelected() {
    if (!QFile::exists(kPdbPath))
        QSKIP("ntkrnlmp.pdb not found at expected path");

    // First enumerate to find _LIST_ENTRY's type index
    QString err;
    QVector<PdbTypeInfo> types = enumeratePdbTypes(kPdbPath, &err);
    QVERIFY2(!types.isEmpty(), qPrintable(err));

    uint32_t listEntryIdx = 0;
    bool found = false;
    for (const auto& t : types) {
        if (t.name == QStringLiteral("_LIST_ENTRY")) {
            listEntryIdx = t.typeIndex;
            found = true;
            break;
        }
    }
    QVERIFY2(found, "_LIST_ENTRY not found in enumeration");

    // Import just _LIST_ENTRY
    QVector<uint32_t> indices = { listEntryIdx };
    int progressCalls = 0;
    NodeTree tree = importPdbSelected(kPdbPath, indices, &err,
        [&](int cur, int total) -> bool {
            progressCalls++;
            Q_UNUSED(total);
            Q_ASSERT(cur <= total);
            return true; // don't cancel
        });
    QVERIFY2(!tree.nodes.isEmpty(), qPrintable(err));
    QVERIFY(progressCalls > 0);

    // Verify _LIST_ENTRY root struct
    int leIdx = findRootStruct(tree, QStringLiteral("_LIST_ENTRY"));
    QVERIFY2(leIdx >= 0, "_LIST_ENTRY should be imported");

    // Flink and Blink
    uint64_t leId = tree.nodes[leIdx].id;
    int flinkIdx = findChildNode(tree, leId, QStringLiteral("Flink"));
    QVERIFY2(flinkIdx >= 0, "Expected 'Flink' in _LIST_ENTRY");
    QCOMPARE(tree.nodes[flinkIdx].kind, NodeKind::Pointer64);

    int blinkIdx = findChildNode(tree, leId, QStringLiteral("Blink"));
    QVERIFY2(blinkIdx >= 0, "Expected 'Blink' in _LIST_ENTRY");
    QCOMPARE(tree.nodes[blinkIdx].kind, NodeKind::Pointer64);

    // Self-referencing pointers
    QCOMPARE(tree.nodes[flinkIdx].refId, leId);
    QCOMPARE(tree.nodes[blinkIdx].refId, leId);

    // Only 1 root struct
    int rootCount = 0;
    for (const auto& n : tree.nodes)
        if (n.parentId == 0 && n.kind == NodeKind::Struct) rootCount++;
    QCOMPARE(rootCount, 1);
}

QTEST_MAIN(TestImportPdb)
#include "test_import_pdb.moc"
