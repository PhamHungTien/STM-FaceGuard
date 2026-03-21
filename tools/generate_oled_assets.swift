import AppKit

struct AssetSpec {
    let name: String
    let text: String
    let font: NSFont
}

let titleFont = NSFont(name: "Verdana-Bold", size: 11) ?? NSFont.boldSystemFont(ofSize: 11)
let mainFont = NSFont(name: "Verdana-Bold", size: 14) ?? NSFont.boldSystemFont(ofSize: 14)
let subFont = NSFont(name: "Verdana", size: 11) ?? NSFont.systemFont(ofSize: 11)

let specs: [AssetSpec] = [
    .init(name: "title", text: "STM-FaceGuard", font: titleFont),
    .init(name: "boot_main", text: "Khởi động", font: mainFont),
    .init(name: "boot_sub", text: "Vui lòng chờ", font: subFont),
    .init(name: "connect_main", text: "Đang kết nối", font: mainFont),
    .init(name: "connect_sub", text: "Chờ ESP32", font: subFont),
    .init(name: "offline_main", text: "Mất kết nối", font: mainFont),
    .init(name: "offline_sub", text: "Kiểm tra CAM/UART", font: subFont),
    .init(name: "ready_main", text: "Sẵn sàng", font: mainFont),
    .init(name: "ready_zero", text: "Nhấn ENROLL", font: subFont),
    .init(name: "ready_saved", text: "Đã lưu", font: subFont),
    .init(name: "db_full_main", text: "Bộ nhớ đầy", font: mainFont),
    .init(name: "db_full_sub", text: "Xóa bớt dữ liệu", font: subFont),
    .init(name: "lock_main", text: "Khóa bảo mật", font: mainFont),
    .init(name: "lock_sub", text: "Quá nhiều lần sai", font: subFont),
    .init(name: "scan_main", text: "Đang quét", font: mainFont),
    .init(name: "scan_sub", text: "Giữ yên khuôn mặt", font: subFont),
    .init(name: "open_main", text: "Mở cửa", font: mainFont),
    .init(name: "open_exit_sub", text: "Nút EXIT", font: subFont),
    .init(name: "denied_main", text: "Từ chối", font: mainFont),
    .init(name: "denied_sub", text: "Không khớp dữ liệu", font: subFont),
    .init(name: "enroll_main", text: "Đăng ký mặt", font: mainFont),
    .init(name: "enroll_sub", text: "Nhìn vào camera", font: subFont),
    .init(name: "step1", text: "B1 Nhìn thẳng", font: mainFont),
    .init(name: "step2", text: "B2 Quay trái", font: mainFont),
    .init(name: "step3", text: "B3 Quay phải", font: mainFont),
    .init(name: "step4", text: "B4 Ngẩng lên", font: mainFont),
    .init(name: "step5", text: "B5 Cúi xuống", font: mainFont),
    .init(name: "enrolled_main", text: "Đã thêm mặt", font: mainFont),
    .init(name: "hold_main", text: "Giữ để xóa", font: mainFont),
    .init(name: "hold_sub", text: "Thả tay để hủy", font: subFont),
    .init(name: "deleting_main", text: "Đang xóa", font: mainFont),
    .init(name: "deleting_sub", text: "Vui lòng chờ", font: subFont),
    .init(name: "deleted_main", text: "Đã xóa hết", font: mainFont),
    .init(name: "deleted_sub", text: "Toàn bộ dữ liệu", font: subFont),
]

func renderBitmap(_ text: String, font: NSFont) -> (width: Int, height: Int, data: [UInt8]) {
    let attr = NSAttributedString(
        string: text,
        attributes: [
            .font: font,
            .foregroundColor: NSColor.black,
        ]
    )

    let size = attr.size()
    let canvasWidth = Int(ceil(size.width)) + 8
    let canvasHeight = Int(ceil(size.height)) + 8

    let image = NSImage(size: NSSize(width: canvasWidth, height: canvasHeight))
    image.lockFocusFlipped(false)
    NSColor.white.setFill()
    NSBezierPath(rect: NSRect(x: 0, y: 0, width: canvasWidth, height: canvasHeight)).fill()
    attr.draw(at: NSPoint(x: 4, y: 4))
    image.unlockFocus()

    guard
        let tiff = image.tiffRepresentation,
        let rep = NSBitmapImageRep(data: tiff)
    else {
        return (0, 0, [])
    }

    func pixelIsOn(x: Int, y: Int) -> Bool {
        let color = rep.colorAt(x: x, y: canvasHeight - 1 - y) ?? .white
        let rgb = color.usingColorSpace(.deviceRGB) ?? .white
        return rgb.redComponent < 0.75 || rgb.greenComponent < 0.75 || rgb.blueComponent < 0.75
    }

    var minX = canvasWidth
    var minY = canvasHeight
    var maxX = -1
    var maxY = -1

    for y in 0..<canvasHeight {
        for x in 0..<canvasWidth where pixelIsOn(x: x, y: y) {
            minX = min(minX, x)
            minY = min(minY, y)
            maxX = max(maxX, x)
            maxY = max(maxY, y)
        }
    }

    if maxX < minX || maxY < minY {
        return (0, 0, [])
    }

    minX = max(0, minX - 1)
    minY = max(0, minY - 1)
    maxX = min(canvasWidth - 1, maxX + 1)
    maxY = min(canvasHeight - 1, maxY + 1)

    let width = maxX - minX + 1
    let height = maxY - minY + 1
    let bytesPerRow = (width + 7) / 8
    var out = [UInt8](repeating: 0, count: bytesPerRow * height)

    for y in 0..<height {
        for x in 0..<width where pixelIsOn(x: minX + x, y: minY + y) {
            let index = y * bytesPerRow + (x / 8)
            out[index] |= UInt8(0x80 >> (x & 7))
        }
    }

    return (width, height, out)
}

print("#ifndef __SSD1306_ASSETS_H")
print("#define __SSD1306_ASSETS_H")
print("")
print("#include <stdint.h>")
print("")
print("typedef struct {")
print("    uint8_t width;")
print("    uint8_t height;")
print("    const uint8_t *data;")
print("} SSD1306_Bitmap;")
print("")

for spec in specs {
    let bitmap = renderBitmap(spec.text, font: spec.font)
    print("static const uint8_t oled_vi_\(spec.name)_data[] = {")

    for chunkStart in stride(from: 0, to: bitmap.data.count, by: 12) {
        let chunk = bitmap.data[chunkStart..<min(chunkStart + 12, bitmap.data.count)]
        let bytes = chunk.map { String(format: "0x%02X", $0) }.joined(separator: ", ")
        print("    \(bytes),")
    }

    print("};")
    print("static const SSD1306_Bitmap oled_vi_\(spec.name) = { \(bitmap.width), \(bitmap.height), oled_vi_\(spec.name)_data };")
    print("")
}

print("#endif /* __SSD1306_ASSETS_H */")
