#pragma once

#include <QSortFilterProxyModel>
#include <QCollator>

class QFileSystemModel;

class FileSortProxyModel final : public QSortFilterProxyModel {
  Q_OBJECT
public:
  explicit FileSortProxyModel(QObject *parent = nullptr);

  void setFoldersFirst(bool on);
  bool foldersFirst() const { return foldersFirst_; }

  void setNaturalSort(bool on);
  bool naturalSort() const { return naturalSort_; }

protected:
  bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
  QFileSystemModel* fsModel() const;

  bool foldersFirst_{true};
  bool naturalSort_{true};
  mutable QCollator collator_;
};
