import QtQuick 2.0
//import QtQuick.Controls 1.3

Text {
    id: label
    width: 64
    height: 24

    FontLoader {
        source: "CBM.ttf"
    }
//    anchors.fill:parent

    font.pixelSize: 16
    font.family: "CBM"
    text: "#0"
}

