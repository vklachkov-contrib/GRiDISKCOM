#ifndef FILEPREVIEW_H
#define FILEPREVIEW_H

#include <QString>
#include <ccos_image/ccos_image.h>

class QWidget;

class FilePreview {
public:
    virtual ~FilePreview() = default;

    virtual QString name() const = 0;

    virtual bool supports(const QString& fileType, size_t fileSize) const = 0;

    virtual QWidget* createWidget(ccos_disk_t* disk, ccos_inode_t* file, QWidget* parent) = 0;
};

#endif  // FILEPREVIEW_H
