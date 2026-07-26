#include "worksheetpreview.h"

#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {

enum RecordType : uint8_t {
    MetadataRecord = 0xFE,  // metadata: title, signature
    LabelRecord    = 0xFD,  // labelled block (column labels / formulas)
};

constexpr uint8_t SignatureByte      = 0x61;
constexpr uint8_t TitleSubtype       = 0x68;
constexpr uint8_t ColumnLabelSubtype = 0x00;

// Sanity guards to prevent garbage from being decoded.
constexpr int kMaxRows = 50000;
constexpr int kMaxCols = 256;

struct Worksheet {
    QString title;
    QMap<int, QString> columnLabels;  // 1-based column index -> label text
    QList<QStringList> rows;          // each row is a list of cell strings
    bool valid = false;               // set only after the signature matches
};

inline uint16_t u16le(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

bool isWorksheetSignature(const uint8_t* data, size_t size) {
    // The opening record of every GRiDPlan II worksheet is `FE 04 00 61 …`
    // (a 4-byte metadata record whose payload begins with the signature byte).
    return size >= 4 && data[0] == MetadataRecord && data[3] == SignatureByte;
}

QList<QStringList> splitCsvRows(const QByteArray& csv) {
    QString text = QString::fromLatin1(csv.constData(), csv.size());
    QStringList lines = text.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty() && csv.endsWith('\n')) {
        lines.removeLast();
    }
    QList<QStringList> rows;
    rows.reserve(lines.size());
    for (const QString& line : lines) {
        QString row = line;
        if (row.endsWith(QLatin1Char('\r'))) {
            row.chop(1);
        }
        rows.append(row.split(QLatin1Char('\t'), Qt::KeepEmptyParts));
    }
    return rows;
}

Worksheet parseWorksheet(const uint8_t* data, size_t size) {
    Worksheet ws;
    if (!isWorksheetSignature(data, size)) {
        printf("Invalid worksheet\n");
        return ws;
    }

    const uint8_t* p = data;
    const uint8_t* end = data + size;

    // Phase 1 — header records. Stop at the first byte that is not a record
    // leader; that byte begins the CSV cell-value block.
    while (end - p >= 3) {
        uint8_t type = *p++;
        if (type != MetadataRecord && type != LabelRecord) {
            break;
        }

        size_t len = u16le(p);
        p += 2;

        if (len < 1) {
            continue;
        }

        const uint8_t* payload = p;
        p += len;

        if (payload + len > end) {
            break;
        }

        uint8_t sub = *payload++;

        if (type == MetadataRecord) {
            if (sub == TitleSubtype) {
                ws.title = QString::fromLatin1(reinterpret_cast<const char*>(payload), int(len - 1));
            }
        } else {  // LabelRecord
            if (sub == ColumnLabelSubtype && len >= 2) {
                int col = *payload++;
                ws.columnLabels[col] = QString::fromLatin1(reinterpret_cast<const char*>(payload), int(len - 2));
            }
        }
    }

    // Phase 2 — data section: gather the raw CSV bytes, skipping over any
    // interleaved FE/FD records (e.g. per-row formula byte-code, FD 0x02).
    QByteArray csv;
    csv.reserve(static_cast<int>(end - p));
    while (p < end) {
        uint8_t type = *p++;
        if (type != MetadataRecord && type != LabelRecord) {
            csv.append(static_cast<char>(type));
            p++;
            continue;
        }

        if (end - p < 3) {
            break;  // truncated leader; leave the tail uncollected
        }

        size_t len = u16le(p);
        p += 2;

        const uint8_t* payload = p;
        p += len;

        if (payload + len > end) {
            // Truncated trailing record — treat the remainder as CSV.
            csv.append(reinterpret_cast<const char*>(p), static_cast<int>(end - p));
            break;
        }
    }

    ws.rows = splitCsvRows(csv);
    ws.valid = true;
    return ws;
}

}  // namespace

bool WorksheetPreview::supports(const QString& fileType, size_t fileSize) const {
    return fileSize >= 4 &&
        fileType.compare("worksheet", Qt::CaseInsensitive) == 0;
}

QWidget* WorksheetPreview::createWidget(ccos_disk_t* disk, ccos_inode_t* file, QWidget* parent) {
    uint8_t* data = nullptr;
    size_t size = 0;
    if (ccos_read_file(disk, file, &data, &size) != CCOS_OK || data == nullptr) {
        QMessageBox::critical(parent, "Preview", "Failed to read file contents!");
        return nullptr;
    }

    Worksheet ws = parseWorksheet(data, size);
    if (!ws.valid) {
        free(data);
        QMessageBox::warning(parent, "Preview", "Could not decode this worksheet (unknown format).");
        return nullptr;
    }
    free(data);

    auto* container = new QWidget(parent);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    if (!ws.title.isEmpty()) {
        auto* titleLabel = new QLabel(ws.title, container);
        QFont titleFont = titleLabel->font();
        titleFont.setBold(true);
        titleLabel->setFont(titleFont);
        titleLabel->setWordWrap(true);
        layout->addWidget(titleLabel);
    }

    int rowCount = qMin(ws.rows.size(), kMaxRows);
    int colCount = 0;
    for (const QStringList& row : ws.rows) {
        colCount = qMax(colCount, row.size());
    }

    for (auto it = ws.columnLabels.cbegin(); it != ws.columnLabels.cend(); ++it) {
        if (it.key() <= kMaxCols) {
            colCount = qMax(colCount, it.key());
        }
    }
    colCount = qMin(colCount, kMaxCols);

    auto* table = new QTableWidget(rowCount, colCount, container);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectItems);
    table->setSelectionMode(QAbstractItemView::ContiguousSelection);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    QFont font("Monospace");
    font.setStyleHint(QFont::TypeWriter);
    table->setFont(font);

    if (!ws.columnLabels.isEmpty()) {
        QStringList headerLabels;
        headerLabels.reserve(colCount);
        for (int c = 0; c < colCount; ++c) {
            headerLabels << ws.columnLabels.value(c + 1);
        }
        table->setHorizontalHeaderLabels(headerLabels);
    } else {
        for (int c = 0; c < colCount; ++c) {
            table->setHorizontalHeaderItem(c, new QTableWidgetItem(QString::number(c + 1)));
        }
    }
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    for (int r = 0; r < rowCount; ++r) {
        const QStringList& row = ws.rows[r];
        for (int c = 0; c < row.size(); ++c) {
            if (!row[c].isEmpty()) {
                table->setItem(r, c, new QTableWidgetItem(row[c]));
            }
        }
    }
    layout->addWidget(table, 1);

    return container;
}
