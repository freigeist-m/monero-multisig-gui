// qml/LockController.qml
pragma Singleton
import QtQuick 2.15

Item {
    id: root
    visible: false

    property bool locked: false

    property int timeoutMinutes: accountManager.lock_timeout_minutes

    Timer {
        id: idleTimer_
        // bind to the minutes; use 1 ms minimum if >0 so interval is never 0
        interval: Math.max(1, timeoutMinutes) * 60 * 1000
        running: timeoutMinutes > 0
        repeat: false
        onTriggered: root.locked = true
    }
    property alias idleTimer: idleTimer_

    function forceLock() {
        locked = true
        idleTimer_.stop()
    }

    function unlock() {
        locked = false
        if (timeoutMinutes > 0) idleTimer_.restart()
    }

    function userActivity() {
        if (!locked && timeoutMinutes > 0)
            idleTimer_.restart()
    }

    onTimeoutMinutesChanged: {
        if (timeoutMinutes > 0) {
            if (!locked) { idleTimer_.stop(); idleTimer_.start(); }
            else idleTimer_.stop()
        } else {
            idleTimer_.stop()
        }
    }

    Component.onCompleted: {
        if (timeoutMinutes > 0) idleTimer_.start()
    }
}
