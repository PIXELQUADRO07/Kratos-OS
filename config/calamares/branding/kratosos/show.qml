import QtQuick 2.0
import QtQuick.Controls 1.0

Rectangle {
    id: presentation
    color: "#1e1e2e"

    Text {
        anchors.centerIn: parent
        text: "Welcome to KratosOS!\n\nInstalling system components..."
        color: "#cdd6f4"
        font.pixelSize: 22
        horizontalAlignment: Text.AlignHCenter
    }
}
