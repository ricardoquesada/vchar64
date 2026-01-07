#include <QBuffer>
#include <QDebug>
#include <QImage>
#include <QtTest>

#include "mock_state.h"
#include "state.h"
#include "stateexport.h"

class TestStateExport : public QObject {
    Q_OBJECT

public:
    TestStateExport();
    ~TestStateExport();

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testExportRaw();
    void testExportPRG();
    void testExportAsm();
    void testExportC();
    void testExportPNG();

private:
    State* m_state;
};

TestStateExport::TestStateExport()
    : m_state(nullptr)
{
}

TestStateExport::~TestStateExport()
{
}

void TestStateExport::initTestCase()
{
    m_state = new State();
}

void TestStateExport::cleanupTestCase()
{
    delete m_state;
}

void TestStateExport::testExportRaw()
{
    // Setup state data
    quint8* charset = m_state->getCharsetBuffer();
    for (int i = 0; i < State::CHAR_BUFFER_SIZE; ++i) {
        charset[i] = (i % 256);
    }

    // Test RAW export of Charset
    QString filename = "test_export.raw";
    qint64 bytesWritten = StateExport::saveRaw(filename, charset, State::CHAR_BUFFER_SIZE);

    QCOMPARE(bytesWritten, State::CHAR_BUFFER_SIZE);

    QFile file(filename);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QByteArray data = file.readAll();
    QCOMPARE(data.size(), State::CHAR_BUFFER_SIZE);

    // Verify content
    for (int i = 0; i < State::CHAR_BUFFER_SIZE; ++i) {
        QCOMPARE((quint8)data.at(i), (quint8)(i % 256));
    }

    file.close();
    file.remove();
}

void TestStateExport::testExportPRG()
{
    // Setup state data
    quint8 buffer[256];
    for (int i = 0; i < 256; ++i)
        buffer[i] = i;

    QString filename = "test_export.prg";
    quint16 address = 0xC000;

    qint64 bytesWritten = StateExport::savePRG(filename, buffer, 256, address);

    // Header (2 bytes) + 256 bytes data
    QCOMPARE(bytesWritten, 258);

    QFile file(filename);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QByteArray data = file.readAll();
    QCOMPARE(data.size(), 258);

    // Check address (Little Endian)
    // 0xC000 -> 00 C0
    QCOMPARE((quint8)data.at(0), 0x00);
    QCOMPARE((quint8)data.at(1), 0xC0);

    // Check data
    for (int i = 0; i < 256; ++i) {
        QCOMPARE((quint8)data.at(2 + i), (quint8)i);
    }

    file.close();
    file.remove();
}

void TestStateExport::testExportAsm()
{
    quint8 buffer[] = { 0x01, 0x02, 0xFF };
    QString filename = "test_export.asm";

    qint64 bytes = StateExport::saveAsm(filename, buffer, 3, "test_label");
    QVERIFY(bytes > 0);

    QFile file(filename);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString content = QString::fromUtf8(file.readAll());

    QVERIFY(content.contains("test_label:"));
    QVERIFY(content.contains(".byte"));
    QVERIFY(content.contains("$01,"));
    QVERIFY(content.contains("$02,"));
    QVERIFY(content.contains("$ff"));

    file.close();
    file.remove();
}

void TestStateExport::testExportC()
{
    quint8 buffer[] = { 0xAA, 0xBB };
    QString filename = "test_export.c";

    qint64 bytes = StateExport::saveC(filename, buffer, 2, "test_array");
    QVERIFY(bytes > 0);

    QFile file(filename);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString content = QString::fromUtf8(file.readAll());

    QVERIFY(content.contains("const char test_array[][16] = {"));
    QVERIFY(content.contains("0xaa, 0xbb"));

    file.close();
    file.remove();
}

void TestStateExport::testExportPNG()
{
    // Create a dummy image
    auto image = std::make_unique<QImage>(10, 10, QImage::Format_ARGB32);
    image->fill(Qt::red);

    QString filename = "test_export.png";
    qint64 bytes = StateExport::savePNG(filename, std::move(image), m_state);

    QVERIFY(bytes > 0);
    QVERIFY(QFile::exists(filename));

    QFile::remove(filename);
}

QTEST_MAIN(TestStateExport)
#include "tst_stateexport.moc"
