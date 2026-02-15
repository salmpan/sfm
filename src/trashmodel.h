#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <QVector>

class TrashModel final : public QAbstractTableModel {
  Q_OBJECT
public:
  enum Column {
    Name = 0,
    OriginalPath = 1,
    DeletedAt = 2,
    Size = 3,
    ColumnCount = 4
  };

  enum Roles {
    TrashedPathRole = Qt::UserRole + 1, // absolute path in Trash/files
    InfoPathRole,
    OriginalPathRole,
    DeletedAtRole,
    IsDirRole,
    SizeRole
  };

  explicit TrashModel(QObject *parent = nullptr);

  void refresh();

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

  QString trashedPathForRow(int row) const;
  QString infoPathForRow(int row) const;
  QString originalPathForRow(int row) const;

  static QString xdgDataHome();
  static QString trashFilesDir();
  static QString trashInfoDir();

  static QString originalPathFromTrashInfo(const QString &trashInfoPath);
  static QDateTime deletedAtFromTrashInfo(const QString &trashInfoPath);

private:
  struct Item {
    QString name;
    QString trashedPath;
    QString infoPath;
    QString originalPath;
    QDateTime deletedAt;
    bool isDir{false};
    qint64 size{0};
  };

  QVector<Item> items_;
};
