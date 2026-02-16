#include <QtTest/QtTest>
#include "core.h"
#include "import_reclass_xml.h"

using namespace rcx;

class TestImportXml : public QObject {
    Q_OBJECT
private slots:
    void importReClassEx();
    void importMemeClsEx();
    void importOlderFormat();
    void importSmallXml();
};

void TestImportXml::importReClassEx() {
    QString path = QStringLiteral("E:/game_dev/dayz/dayz2.reclass");
    QFile f(path);
    if (!f.exists()) { QSKIP("dayz2.reclass not found"); return; }

    QString error;
    NodeTree tree = importReclassXml(path, &error);
    QVERIFY2(!tree.nodes.isEmpty(), qPrintable(error));

    // Count root structs
    int rootCount = 0;
    for (const auto& n : tree.nodes)
        if (n.parentId == 0 && n.kind == NodeKind::Struct) rootCount++;
    QVERIFY(rootCount > 0);
    qDebug() << "dayz2.reclass:" << rootCount << "classes," << tree.nodes.size() << "nodes";

    // First root should be collapsed
    QCOMPARE(tree.nodes[0].collapsed, true);

    // Verify pointer resolution
    int resolved = 0;
    for (const auto& n : tree.nodes) {
        if ((n.kind == NodeKind::Pointer64 || n.kind == NodeKind::Pointer32) && n.refId != 0)
            resolved++;
    }
    QVERIFY(resolved > 0);
    qDebug() << "  Resolved pointers:" << resolved;

    // Check specific known class exists
    bool hasAVWorld = false;
    for (const auto& n : tree.nodes) {
        if (n.parentId == 0 && n.name == QStringLiteral("AVWorld")) {
            hasAVWorld = true;
            break;
        }
    }
    QVERIFY(hasAVWorld);
}

void TestImportXml::importMemeClsEx() {
    QString path = QStringLiteral("E:/game_dev/dayz/dayz3.MemeCls");
    QFile f(path);
    if (!f.exists()) { QSKIP("dayz3.MemeCls not found"); return; }

    QString error;
    NodeTree tree = importReclassXml(path, &error);
    QVERIFY2(!tree.nodes.isEmpty(), qPrintable(error));

    int rootCount = 0;
    for (const auto& n : tree.nodes)
        if (n.parentId == 0 && n.kind == NodeKind::Struct) rootCount++;
    QVERIFY(rootCount > 0);
    qDebug() << "dayz3.MemeCls:" << rootCount << "classes," << tree.nodes.size() << "nodes";
}

void TestImportXml::importOlderFormat() {
    QString path = QStringLiteral("E:/game_dev/dayz/dayz.reclass");
    QFile f(path);
    if (!f.exists()) { QSKIP("dayz.reclass not found"); return; }

    QString error;
    NodeTree tree = importReclassXml(path, &error);
    QVERIFY2(!tree.nodes.isEmpty(), qPrintable(error));

    int rootCount = 0;
    for (const auto& n : tree.nodes)
        if (n.parentId == 0 && n.kind == NodeKind::Struct) rootCount++;
    QVERIFY(rootCount > 0);
    qDebug() << "dayz.reclass:" << rootCount << "classes," << tree.nodes.size() << "nodes";
}

void TestImportXml::importSmallXml() {
    // Create a minimal XML in a temp file and test parsing
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    QVERIFY(tmp.open());
    tmp.write(R"(<?xml version="1.0" encoding="UTF-8"?>
<ReClass>
    <!--ReClassEx-->
    <Class Name="TestClass" Type="28" Comment="" Offset="0" strOffset="0" Code="">
        <Node Name="vtable" Type="9" Size="8" bHidden="false" Comment=""/>
        <Node Name="health" Type="13" Size="4" bHidden="false" Comment=""/>
        <Node Name="name" Type="18" Size="32" bHidden="false" Comment=""/>
        <Node Name="position" Type="23" Size="12" bHidden="false" Comment=""/>
        <Node Name="pNext" Type="8" Size="8" bHidden="false" Comment="" Pointer="TestClass"/>
    </Class>
</ReClass>
)");
    tmp.flush();

    QString error;
    NodeTree tree = importReclassXml(tmp.fileName(), &error);
    QVERIFY2(!tree.nodes.isEmpty(), qPrintable(error));

    // Should have 1 root struct + 5 children = 6 nodes
    QCOMPARE(tree.nodes.size(), 6);

    // Root struct
    QCOMPARE(tree.nodes[0].kind, NodeKind::Struct);
    QCOMPARE(tree.nodes[0].name, QStringLiteral("TestClass"));

    // vtable = Int64
    QCOMPARE(tree.nodes[1].kind, NodeKind::Int64);
    QCOMPARE(tree.nodes[1].name, QStringLiteral("vtable"));
    QCOMPARE(tree.nodes[1].offset, 0);

    // health = Float
    QCOMPARE(tree.nodes[2].kind, NodeKind::Float);
    QCOMPARE(tree.nodes[2].name, QStringLiteral("health"));
    QCOMPARE(tree.nodes[2].offset, 8);

    // name = UTF8 with strLen=32
    QCOMPARE(tree.nodes[3].kind, NodeKind::UTF8);
    QCOMPARE(tree.nodes[3].strLen, 32);
    QCOMPARE(tree.nodes[3].offset, 12);

    // position = Vec3
    QCOMPARE(tree.nodes[4].kind, NodeKind::Vec3);
    QCOMPARE(tree.nodes[4].offset, 44);

    // pNext = Pointer64 with resolved refId
    QCOMPARE(tree.nodes[5].kind, NodeKind::Pointer64);
    QCOMPARE(tree.nodes[5].name, QStringLiteral("pNext"));
    QVERIFY(tree.nodes[5].refId != 0);
    QCOMPARE(tree.nodes[5].refId, tree.nodes[0].id); // points to TestClass
}

QTEST_MAIN(TestImportXml)
#include "test_import_xml.moc"
