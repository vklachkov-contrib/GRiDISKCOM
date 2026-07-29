#ifndef PARTITIONDLG_H
#define PARTITIONDLG_H

#include <QDialog>
#include <QSet>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <vector>

namespace Ui {
class PartitionDlg;
}

struct MbrPartition {
    size_t   index;
    bool     isGRiD;
    bool     isActive;
    uint64_t offset;
    uint64_t size;
};

class PartitionDlg : public QDialog
{
    Q_OBJECT

public:
    explicit PartitionDlg(QWidget *parent = nullptr);
    ~PartitionDlg();

    void setTitle(const QString &text);
    void setInfo(const QString &text);

    // `labels` is parallel to `parts`; empty strings are allowed.
    void setPartitions(const std::vector<MbrPartition> &parts,
                       const QStringList &labels,
                       const QSet<int> &disabledSlots = {});

    // Prepends a selectable entry before the partitions; selectedIndex()
    // returns -1 when it is chosen.
    void allowNone(const QString &noneText = {});

    int selectedIndex() const;

private:
    void addRow(const QString &text, bool enabled);

    Ui::PartitionDlg *ui;
    bool m_hasNone = false;
    QString m_noneText;
};

#endif // PARTITIONDLG_H
