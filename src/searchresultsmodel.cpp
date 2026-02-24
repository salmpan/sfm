#include "searchresultsmodel.h"

#include <QFileInfo>
#include <QLocale>

SearchResultsModel::SearchResultsModel(QObject *parent)
  : QAbstractTableModel(parent) {}

int SearchResultsModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid()) return 0;
  return items_.size();
}

int SearchResultsModel::columnCount(const QModelIndex &parent) const {
  if (parent.isValid()) return 0;
  return ColumnCount;
}

static QString prettySize(qint64 bytes) {
  if (bytes < 0) return QString();
  return QLocale().formattedDataSize(bytes);
}

QVariant SearchResultsModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid()) return {};
  const auto &it = items_.at(index.row());
  const int c = index.column();

  if (role == Qt::DisplayRole) {
    switch (c) {
      case Name: {
        return QFileInfo(it.path).fileName();
      }
      case Path:
        return it.path;
      case Line:
        if (it.line > 0) {
          if (it.column > 0) return QString::number(it.line) + ":" + QString::number(it.column);
          return QString::number(it.line);
        }
        return QString();
      case Preview:
        return it.preview;
      case Size:
        return prettySize(it.size);
      case Modified:
        return it.modified.isValid() ? QLocale().toString(it.modified, QLocale::ShortFormat) : QString();
    }
  }

  if (role == Qt::ToolTipRole) {
    if (c == Preview && !it.preview.isEmpty()) return it.preview;
    if (c == Path) return it.path;
  }

  if (role == Qt::TextAlignmentRole) {
    if (c == Size) return Qt::AlignRight;
  }

  return {};
}

QVariant SearchResultsModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
  switch (section) {
    case Name: return tr("Name");
    case Path: return tr("Path");
    case Line: return tr("Line");
    case Preview: return tr("Match");
    case Size: return tr("Size");
    case Modified: return tr("Modified");
  }
  return {};
}

void SearchResultsModel::clear() {
  beginResetModel();
  items_.clear();
  endResetModel();
}

void SearchResultsModel::addResult(const SearchResultItem &item) {
  const int row = items_.size();
  beginInsertRows(QModelIndex(), row, row);
  items_.push_back(item);
  endInsertRows();
}

const SearchResultItem& SearchResultsModel::itemAt(int row) const {
  return items_.at(row);
}
