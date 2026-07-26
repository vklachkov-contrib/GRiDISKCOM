#include "textpreview.h"

#include <QPlainTextEdit>
#include <QMessageBox>
#include <QString>

// Decode file bytes as Latin-1, rendering control characters as Unicode
// control-picture glyphs (NUL -> ␀, BEL -> ␇, ESC -> ␛, DEL -> ␡, etc.) so they
// are visible instead of blank. Tab and newline are kept as-is to preserve layout.
static QString decodeText(const uint8_t* data, size_t size) {
    QString out;
    out.reserve(static_cast<int>(size));
    for (size_t i = 0; i < size; i++) {
        unsigned char c = data[i];
        if (c == '\t') {
            out += QLatin1Char('\t');
        } else if (c == '\n') {
            out += QChar(static_cast<ushort>(0x2400 + c));
            out += QLatin1Char('\n');
        } else if (c < 0x20) {
            out += QChar(static_cast<ushort>(0x2400 + c));
        } else if (c == 0x7F) {
            out += QChar(static_cast<ushort>(0x2421));  // SYMBOL FOR DELETE
        } else {
            out += QLatin1Char(static_cast<char>(c));
        }
    }
    return out;
}

bool TextPreview::supports(const QString& fileType, size_t fileSize) const {
    (void)fileSize;
    return fileType.compare("text", Qt::CaseInsensitive) == 0;
}

QWidget* TextPreview::createWidget(ccos_disk_t* disk, ccos_inode_t* file, QWidget* parent) {
    uint8_t* data = nullptr;
    size_t size = 0;
    if (ccos_read_file(disk, file, &data, &size) != CCOS_OK || data == nullptr) {
        QMessageBox::critical(parent, "Preview", "Failed to read file contents!");
        return nullptr;
    }

    auto* edit = new QPlainTextEdit(parent);
    edit->setReadOnly(true);
    edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont font("Monospace");
    font.setStyleHint(QFont::TypeWriter);
    edit->setFont(font);

    // The first `prop_length` bytes of the file store properties and are not part of the text.
    uint32_t prop_length = file->desc.prop_length;
    size_t offset = (prop_length <= size) ? prop_length : size;
    size_t text_len = size - offset;
    edit->setPlainText(decodeText(data + offset, text_len));

    free(data);
    return edit;
}
