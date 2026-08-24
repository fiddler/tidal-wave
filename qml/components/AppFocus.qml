pragma Singleton
import QtQuick

// Whether the user is actually looking at the app.
//
// A Qt Quick window renders frames only while something animates, so the
// perpetual spinners are the render loop's only reason to run at rest. Left
// ungated they cost a full frame per display refresh — measured at 18-22% CPU
// on an idle window sitting behind other apps — for pixels nobody is looking
// at. Every `loops: Animation.Infinite` in this app is gated on this and on
// its own item being visible, which is what puts the app back at zero frames
// per second when it is idle.
QtObject {
    readonly property bool active: Qt.application.state === Qt.ApplicationActive
}
