#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <QVector>

struct SearchResultItem {
  QString path;       // absolute
  bool isDir{false};

  // Content matches
  int line{0};        // 1-based; 0 when N/A
  int column{0};      // 1-based; 0 when N/A
  QString preview;    // matching line (trimmed)

  qint64 size{-1};
  QDateTime modified;
};

class SearchResultsModel final : public QAbstractTableModel {
  Q_OBJECT
public:
  enum Column {
    Name = 0,
    Path,
    Line,
    Preview,
    Size,
    Modified,
    ColumnCount
  };

  explicit SearchResultsModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

  void clear();
  void addResult(const SearchResultItem &item);
  const SearchResultItem& itemAt(int row) const;

private:
  QVector<SearchResultItem> items_;
};
