#include "mock_state.h"
#include "stateimport.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

class StateImportTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testLoadValidVSF();
    void testLoadInvalidVSF();
    void testLoadValidVChar64Proj();
    void testLoadInvalidVChar64Proj();

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

void StateImportTest::testLoadValidVChar64Proj()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        QSKIP("Could not create temporary directory");
    }

    QString filePath = tempDir.filePath("test.vchar64proj");
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));

    StateImport::VChar64Header header;
    memset(&header, 0, sizeof(header));
    memcpy(header.id, "VChar", 5);
    header.version = 3;
    header.num_chars = qToLittleEndian((quint16)256);
    header.tile_width = 1;
    header.tile_height = 1;
    header.map_width = qToLittleEndian((quint16)40);
    header.map_height = qToLittleEndian((quint16)25);
    header.color_mode = 1; // Per Tile

    file.write((const char*)&header, sizeof(header));

    // Write Charset (256 * 8 bytes)
    QByteArray charset(256 * 8, 0xEE);
    file.write(charset);

    // Write Tile Colors (256 bytes)
    QByteArray tileColors(256, 0x05);
    file.write(tileColors);

    // Write Map (40 * 25 bytes)
    QByteArray mapData(40 * 25, 0x01);
    file.write(mapData);

    file.close();

    // Now load it
    QVERIFY(file.open(QIODevice::ReadOnly));
    State state;
    qint64 bytesRead = StateImport::loadVChar64(&state, file);

    QVERIFY(bytesRead > 0);
    QCOMPARE(state._mapSize, QSize(40, 25));
    QCOMPARE(state._tileProperties.size, QSize(1, 1));
    // Check if some data was loaded correctly (checking first byte of charset)
    QCOMPARE(state._charset[0], 0xEE);
    QCOMPARE(state._tileColors[0], 0x05);
}

void StateImportTest::testLoadInvalidVChar64Proj()
{
    State state;
    QTemporaryDir tempDir;

    // 1. File too small
    {
        QString path = tempDir.filePath("small.vchar64proj");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("VCha", 4);
        file.close();

        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(StateImport::loadVChar64(&state, file), -1);
    }

    // 2. Invalid ID
    {
        QString path = tempDir.filePath("invalid_id.vchar64proj");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        StateImport::VChar64Header header;
        memset(&header, 0, sizeof(header));
        memcpy(header.id, "XChar", 5); // Invalid
        file.write((const char*)&header, sizeof(header));
        file.close();

        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(StateImport::loadVChar64(&state, file), -1);
    }

    // 3. Unsupported Version
    {
        QString path = tempDir.filePath("bad_version.vchar64proj");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        StateImport::VChar64Header header;
        memset(&header, 0, sizeof(header));
        memcpy(header.id, "VChar", 5);
        header.version = 99; // Unsupported
        file.write((const char*)&header, sizeof(header));
        file.close();

        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(StateImport::loadVChar64(&state, file), -1);
    }
}

QTEST_MAIN(StateImportTest)
#include "tst_stateimport.moc"
