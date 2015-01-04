import QtQuick 2.4
import QtQuick.Controls 1.3
import QtQuick.Layouts 1.1

Item {

    property alias prevCharButton: prevChar
    property alias nextCharButton: nextChar
    property alias bigChar: bigChar

    width: 1040
    height: 600

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

    CharSet {
        id: charSet1
        x: 8
        y: 316
        width: 1024
        height: 164
    }
}
