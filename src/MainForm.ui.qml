import QtQuick 2.4
import QtQuick.Controls 1.3
import QtQuick.Layouts 1.1

Item {
    id: item1

    property alias prevCharButton: prevChar
    property alias nextCharButton: nextChar
    property alias bigChar: bigChar
    property alias charIndexLabel: cbmLabel1

    width: 640
    height: 480

    RowLayout {
        id: rowLayout1
        x: 8
        y: 270
        width: 94
        height: 33

        Button {
            id: prevChar
            text: qsTr("<")
        }

        Button {
            id: nextChar
            text: qsTr(">")
        }
    }

    BigChar {
        id: bigChar
        x: 8
        y: 8
    }

    CharSetView {
        id: charSet1
        x: 8
        y: 316
        width: 512
        height: 128
    }

    CMBLabel {
        id: cbmLabel1
        x: 200
        y: 276
        width: 64
        height: 21
    }
}
