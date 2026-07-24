#include "canvaspreview.h"

#include <QLabel>
#include <QImage>
#include <QPixmap>
#include <QMessageBox>

static const int CANVAS_W = 320;
static const int CANVAS_H = 240;
static const int BYTES_PER_ROW = CANVAS_W / 8;
static const size_t CANVAS_SIZE = 9600;

bool CanvasPreview::supports(const QString& fileType, size_t fileSize) const {
    bool const canvas = fileType.compare("canvas", Qt::CaseInsensitive) == 0 && fileSize == CANVAS_SIZE;
    bool const scrimage = fileType.compare("screenimage", Qt::CaseInsensitive) == 0;
    return canvas || scrimage;
}

QWidget* CanvasPreview::createWidget(ccos_disk_t* disk, ccos_inode_t* file, QWidget* parent) {
    uint8_t* data = nullptr;
    size_t size = 0;
    if (ccos_read_file(disk, file, &data, &size) != CCOS_OK || data == nullptr) {
        QMessageBox::critical(parent, "Preview", "Failed to read file contents!");
        return nullptr;
    }

    QImage img(CANVAS_W, CANVAS_H, QImage::Format_RGB32);
    img.fill(qRgb(0, 0, 0));

    int height = static_cast<int>(size / BYTES_PER_ROW);
    for (int y = 0; y < height; y++) {
        const uint8_t* row = data + y * BYTES_PER_ROW;
        QRgb* scan = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < CANVAS_W; x++) {
            int wordIdx = x / 16;
            uint16_t word = static_cast<uint16_t>(row[wordIdx * 2] | (row[wordIdx * 2 + 1] << 8));
            int bit = 15 - (x % 16);
            bool on = (word >> bit) & 1;
            scan[x] = on ? qRgb(255, 255, 255) : qRgb(0, 0, 0);
        }
    }
    free(data);

    auto* label = new QLabel(parent);
    label->setPixmap(QPixmap::fromImage(img).scaled(CANVAS_W * 2, CANVAS_H * 2,
                                                    Qt::KeepAspectRatio, Qt::FastTransformation));
    label->setAlignment(Qt::AlignCenter);
    return label;
}
