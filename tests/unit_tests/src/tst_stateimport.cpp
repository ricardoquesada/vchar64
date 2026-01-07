
#include "stateimport.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class StateImportTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testLoadValidVSF();
    void testLoadInvalidVSF();

private:
    QString m_validVSFPath;
};

void StateImportTest::initTestCase()
{
    // Point to a known valid VSF file provided in the repository
    // Assuming the test is run from a build directory parallel to source
    // We might need to adjust the path or copy the file
    m_validVSFPath = QCoreApplication::applicationDirPath() + "/../../tests/bluemax.vsf";

    if (!QFile::exists(m_validVSFPath)) {
        // Fallback to source location if running from build dir
        m_validVSFPath = QString(SOURCE_DIR) + "/tests/bluemax.vsf";
    }
}

void StateImportTest::cleanupTestCase()
{
}

void StateImportTest::testLoadValidVSF()
{
    QFile file(m_validVSFPath);
    QVERIFY2(file.exists(), "Test file bluemax.vsf does not exist");

    quint8 buffer64k[65536] = { 0 };
    quint16 charsetAddress = 0;
    quint16 screenRAMAddress = 0;
    quint8 colorRAM[1024] = { 0 };
    quint8 vicRegisters[64] = { 0 };

    qint64 result = StateImport::parseVICESnapshot(
        file,
        buffer64k,
        &charsetAddress,
        &screenRAMAddress,
        colorRAM,
        vicRegisters);

    QCOMPARE(result, 0); // parseVICESnapshot returns 0 on success

    // Basic validation that something was loaded
    // Check if charset address looks reasonable (bluemax might have specific address, but it should be non-zero usually)
    // Actually, it might be 0 if the game uses bank 0 and offset 0.
    // Let's check if the buffer is not completely empty
    bool bufferNotEmpty = false;
    for (int i = 0; i < 65536; ++i) {
        if (buffer64k[i] != 0) {
            bufferNotEmpty = true;
            break;
        }
    }
    QVERIFY(bufferNotEmpty);
}

void StateImportTest::testLoadInvalidVSF()
{
    // Test with a non-existent file
    QFile file("non_existent_file.vsf");

    quint8 buffer64k[65536];
    quint16 charsetAddress;
    quint16 screenRAMAddress;
    quint8 colorRAM[1024];
    quint8 vicRegisters[64];

    qint64 result = StateImport::parseVICESnapshot(
        file,
        buffer64k,
        &charsetAddress,
        &screenRAMAddress,
        colorRAM,
        vicRegisters);

    QVERIFY(result < 0);
}

QTEST_MAIN(StateImportTest)
#include "tst_stateimport.moc"
