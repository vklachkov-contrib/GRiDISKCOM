#include "previewdlg.h"

#include "filepreview.h"
#include "textpreview.h"
#include "canvaspreview.h"
#include "worksheetpreview.h"

#include <QVBoxLayout>
#include <QLabel>

static QList<FilePreview*> previewRegistry() {
    static QList<FilePreview*> registry = {
        new TextPreview(),
        new CanvasPreview(),
        new WorksheetPreview(),
    };
    return registry;
}

PreviewDlg::PreviewDlg(ccos_disk_t* disk, ccos_inode_t* file, QWidget* parent) : QDialog(parent) {
    setWindowTitle("File preview");
    setModal(true);
    setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    setSizeGripEnabled(false);

    auto* layout = new QVBoxLayout(this);

    char basename[CCOS_MAX_FILE_NAME] = {0};
    char type[CCOS_MAX_FILE_NAME] = {0};
    ccos_parse_file_name(file, basename, type, nullptr, nullptr);
    size_t fileSize = file->desc.file_size;

    FilePreview* matched = nullptr;
    for (FilePreview* p : previewRegistry()) {
        if (p->supports(QString::fromLatin1(type), fileSize)) {
            matched = p;
            break;
        }
    }

    QWidget* content = nullptr;
    if (matched) {
        content = matched->createWidget(disk, file, this);
    }
    if (content == nullptr) {
        content = new QLabel(QString("No preview available for \"~%1~\".")
                                 .arg(QString::fromLatin1(type)), this);
        static_cast<QLabel*>(content)->setAlignment(Qt::AlignCenter);
    }
    layout->addWidget(content, 1);

    setFixedSize(660, 540);
}

void PreviewDlg::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (auto* p = parentWidget()) {
        move(p->geometry().center() - QPoint(width() / 2, height() / 2));
    }
}
