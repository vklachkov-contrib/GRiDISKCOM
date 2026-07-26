#include "canvaspreview.h"

#include <QLabel>
#include <QImage>
#include <QPixmap>
#include <QMessageBox>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace {

enum RecordType : uint8_t {
    MetadataRecord = 0xFE,  // metadata / dimensions
    LabelRecord    = 0xFD,  // labelled block (owner / font / objects)
};

// Sanity guards to prevent garbage from being decoded.
constexpr int kMaxImageWidth  = 8192;
constexpr int kMaxImageHeight = 4096;

struct CanvasImage {
    QString format;
    int w = 0;
    int h = 0;
    const uint8_t* raster = nullptr;
    size_t rasterLen = 0;
};

inline uint16_t u16le(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

inline size_t rasterBytes(int w, int h) {
    return size_t(w / 8) * size_t(h);
}

bool plausibleDims(int w, int h) {
    // Width must be a whole number of 16-bit words: the pixel packing reads
    // two bytes per 16 pixels, so a non-multiple-of-16 width would misalign.
    return w > 0 && w <= kMaxImageWidth && h > 0 && h <= kMaxImageHeight && (w % 16) == 0;
}

bool isGridpaintSignature(const uint8_t* data, size_t size) {
    return size >= 6 && data[0] == MetadataRecord &&
        data[3] == 0x61 && data[4] == 0xDE && data[5] == 0x52;
}

CanvasImage decodeGridpaint(const uint8_t* data, size_t size) {
    const uint8_t* p = data;
    const uint8_t* end = data + size;

    while (end - p >= 3) {
        uint8_t type = *p++;

        size_t len = u16le(p);
        p += 2;

        if (type != MetadataRecord && type != LabelRecord) {
            break;
        }

        if (p + len > end) {
            break;
        }

        const uint8_t* payload = p;
        p += len;

        // The dimensions record is a MetadataRecord with a 4-byte payload
        // (u16 width + u16 height), the raster follows it directly.
        //
        // The only other 4-byte MetadataRecord is the signature, whose payload fails
        // plausibleDims, so the first match here is the real dims record.
        if (type == MetadataRecord && len == 4) {
            int w = u16le(payload);
            int h = u16le(payload + 2);
            size_t expect = rasterBytes(w, h);

            const uint8_t* r = p;
            if (r < end && *r == 0xFF) {
                r++;  // skip the block marker
            }

            if (plausibleDims(w, h) && size_t(end - r) >= expect) {
                CanvasImage img;
                img.format = QStringLiteral("gridpaint");
                img.w = w;
                img.h = h;
                img.raster = r;
                img.rasterLen = expect;
                return img;
            }
        }
    }

    return CanvasImage{};
}

CanvasImage decodeRawWithHeader(const uint8_t* data, size_t size) {
    const uint8_t* p = data;
    const uint8_t* end = data + size;

    if (end - p < 4) {
        return CanvasImage{};
    }

    int w = u16le(p); p += 2;
    int h = u16le(p); p += 2;

    size_t expect = rasterBytes(w, h);
    if (plausibleDims(w, h) && size_t(end - p) == expect) {
        CanvasImage img;
        img.format = QStringLiteral("raw");
        img.w = w;
        img.h = h;
        img.raster = p;
        img.rasterLen = expect;
        return img;
    }

    return CanvasImage{};
}

CanvasImage decodeRawHeaderless(const uint8_t* data, size_t size) {
    const size_t compassScreenWidth = 320;

    const size_t bytesPerRow = compassScreenWidth / 8;
    if (size == 0 || size % bytesPerRow != 0) {
        return CanvasImage{};
    }

    int h = static_cast<int>(size / bytesPerRow);
    if (h <= 0 || h > kMaxImageHeight) {
        return CanvasImage{};
    }

    CanvasImage img;
    img.format = QStringLiteral("raw");
    img.w = compassScreenWidth;
    img.h = h;
    img.raster = data;
    img.rasterLen = size;

    return img;
}

CanvasImage detectCanvas(const uint8_t* data, size_t size) {
    if (isGridpaintSignature(data, size)) {
        return decodeGridpaint(data, size);
    }
    if (CanvasImage img = decodeRawWithHeader(data, size); img.raster) {
        return img;
    }
    return decodeRawHeaderless(data, size);
}

QImage renderMono(const uint8_t* raster, size_t rasterLen, int w, int h) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(qRgb(0, 0, 0));

    const int bytesPerRow = w / 8;
    const int rows = std::min(h, static_cast<int>(rasterLen / bytesPerRow));
    const QRgb on = qRgb(255, 255, 255);
    const QRgb off = qRgb(0, 0, 0);

    for (int y = 0; y < rows; y++) {
        const uint8_t* row = raster + y * bytesPerRow;
        QRgb* scan = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; x++) {
            const int bo = (x / 16) * 2;
            uint16_t word = static_cast<uint16_t>(row[bo] | (row[bo + 1] << 8));
            scan[x] = (word >> (15 - (x % 16))) & 1 ? on : off;
        }
    }
    return img;
}

// QLabel that rescales its pixmap to fit on resize, keeping aspect ratio.
// FastTransformation keeps the 1-bpp pixel art crisp instead of smearing it.
class ImageLabel : public QLabel {
public:
    using QLabel::QLabel;

    void setSourceImage(const QImage& image) {
        source = image;
        rescale(size());
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        rescale(event->size());
    }

private:
    void rescale(const QSize& area) {
        if (source.isNull()) {
            return;
        }
        setPixmap(QPixmap::fromImage(
            source.scaled(area, Qt::KeepAspectRatio, Qt::FastTransformation)));
    }

    QImage source;
};

}  // namespace

bool CanvasPreview::supports(const QString& fileType, size_t fileSize) const {
    return fileSize >= 4 &&
        (fileType.compare("canvas", Qt::CaseInsensitive) == 0 ||
         fileType.compare("screenimage", Qt::CaseInsensitive) == 0);
}

QWidget* CanvasPreview::createWidget(ccos_disk_t* disk, ccos_inode_t* file, QWidget* parent) {
    uint8_t* data = nullptr;
    size_t size = 0;
    if (ccos_read_file(disk, file, &data, &size) != CCOS_OK || data == nullptr) {
        QMessageBox::critical(parent, "Preview", "Failed to read file contents!");
        return nullptr;
    }

    CanvasImage img = detectCanvas(data, size);
    if (img.raster == nullptr) {
        free(data);
        QMessageBox::warning(parent, "Preview", "Could not decode this canvas image (unknown format).");
        return nullptr;
    }

    QImage image = renderMono(img.raster, img.rasterLen, img.w, img.h);
    free(data);

    auto* container = new QWidget(parent);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto* imageLabel = new ImageLabel(container);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setSourceImage(image);
    layout->addWidget(imageLabel, 1);

    auto* info = new QLabel(
        QStringLiteral("Format: %1    |    Resolution: %2 × %3").arg(img.format).arg(img.w).arg(img.h),
        container);
    info->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(info);

    return container;
}
