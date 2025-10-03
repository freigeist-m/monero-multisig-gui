import QtQuick 2.15
import "../../js/qrcode.js" as QR


Item {
    id: root
    property alias value: qrData.text
    property string ecc: "L"          // L, M, Q, H
    property int size: 320            // target pixels
    property int margin: 4            // quiet zone (modules)
    property color moduleColor: "black"
    property color backgroundColor: "white"

    function toDataUrl() { return canvas.toDataURL("image/png"); }

    width: size; height: size

    Text { id: qrData; visible: false }

    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: false
        renderTarget: Canvas.Image
        onPaint: {
            var ctx = getContext("2d");

            if (ctx.reset) ctx.reset();
            ctx.fillStyle = backgroundColor;
            ctx.fillRect(0, 0, width, height);
            ctx.imageSmoothingEnabled = false;

            var data = qrData.text || "";
            if (!data) return;


            var qr = QR.qrcode(0, ecc);
            qr.addData(data);
            qr.make();

            var count = qr.getModuleCount();
            if (count <= 0) return;

            var totalModules = count + margin * 2;
            var scale = Math.max(1, Math.floor(Math.min(width, height) / totalModules));
            var dim = totalModules * scale;

            var ox = Math.floor((width  - dim) / 2);
            var oy = Math.floor((height - dim) / 2);

            ctx.fillStyle = moduleColor;

            for (var r = 0; r < count; r++) {
                for (var c = 0; c < count; c++) {
                    if (qr.isDark(r, c)) {
                        var x = ox + (c + margin) * scale;
                        var y = oy + (r + margin) * scale;
                        ctx.fillRect(x, y, scale, scale);
                    }
                }
            }
        }

        onWidthChanged: requestPaint();
        onHeightChanged: requestPaint();
    }

    onValueChanged: canvas.requestPaint()
    onEccChanged: canvas.requestPaint()
    onSizeChanged: canvas.requestPaint()
    onMarginChanged: canvas.requestPaint()
    onModuleColorChanged: canvas.requestPaint()
    onBackgroundColorChanged: canvas.requestPaint()
}
