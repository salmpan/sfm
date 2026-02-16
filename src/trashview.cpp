#include "trashview.h"

#include <QAbstractItemView>
#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QTreeView>
#include <QDesktopServices>
#include <QUrl>
#include <QItemSelectionModel>

#include "trashmodel.h"

namespace {

class TrashSortProxy final : public QSortFilterProxyModel {
public:
  explicit TrashSortProxy(QObject *parent = nullptr)
    : QSortFilterProxyModel(parent) {
    collator_.setCaseSensitivity(Qt::CaseInsensitive);
    collator_.setNumericMode(true);
    setDynamicSortFilter(true);
  }

  void setFoldersFirst(bool on) {
    foldersFirst_ = on;
    invalidate();
  }

protected:
  bool lessThan(const QModelIndex &left, const QModelIndex &right) const override {
    if (!sourceModel()) return false;
    if (!left.isValid() || !right.isValid()) return false;
    if (left.row() < 0 || right.row() < 0) return false;

    const QModelIndex l0 = left.sibling(left.row(), 0);
    const QModelIndex r0 = right.sibling(right.row(), 0);
    if (!l0.isValid() || !r0.isValid()) return false;

    const bool ld = sourceModel()->data(left.sibling(left.row(), 0), TrashModel::IsDirRole).toBool();
    const bool rd = sourceModel()->data(right.sibling(right.row(), 0), TrashModel::IsDirRole).toBool();


    if (foldersFirst_ && ld != rd) return ld;

    const int col = left.column();
    if (col == TrashModel::Name) {
      const QString ln = sourceModel()->data(left, Qt::DisplayRole).toString();
      const QString rn = sourceModel()->data(right, Qt::DisplayRole).toString();
      return collator_.compare(ln, rn) < 0;
    }

    if (col == TrashModel::DeletedAt) {
      const auto lt = sourceModel()->data(left, TrashModel::DeletedAtRole).toDateTime();
      const auto rt = sourceModel()->data(right, TrashModel::DeletedAtRole).toDateTime();
      if (lt != rt) return lt < rt;
    }

    if (col == TrashModel::Size) {
      const qint64 ls = sourceModel()->data(left, TrashModel::SizeRole).toLongLong();
      const qint64 rs = sourceModel()->data(right, TrashModel::SizeRole).toLongLong();
      if (ls != rs) return ls < rs;
    }

    // Fallback: display role string compare
    const QString l = sourceModel()->data(left, Qt::DisplayRole).toString();
    const QString r = sourceModel()->data(right, Qt::DisplayRole).toString();
    const int c = collator_.compare(l, r);
    if (c != 0) return c < 0;

    // Stable fallback by name
    const QString ln = sourceModel()->data(left.sibling(left.row(), 0), Qt::DisplayRole).toString();
    const QString rn = sourceModel()->data(right.sibling(right.row(), 0), Qt::DisplayRole).toString();
    return collator_.compare(ln, rn) < 0;
  }

private:
  bool foldersFirst_{true};
  mutable QCollator collator_;
};

static bool removePathRecursively(const QString &p, QString *err) {
  QFileInfo fi(p);
  if (!fi.exists()) return true;
  if (fi.isDir() && !fi.isSymLink()) {
    QDir d(p);
    if (!d.removeRecursively()) {
      if (err) *err = "Failed to remove directory:\n" + p;
      return false;
    }
    return true;
  }
  if (!QFile::remove(p)) {
    if (err) *err = "Failed to remove file:\n" + p;
    return false;
  }
  return true;
}

} // namespace

TrashView::TrashView(QWidget *parent)
  : QWidget(parent) {
  model_ = new TrashModel(this);
  proxy_ = new TrashSortProxy(this);
  proxy_->setSourceModel(model_);

  stack_ = new QStackedWidget(this);

  // Detailed list
  listView_ = new QTreeView(stack_);
  listView_->setModel(proxy_);
  listView_->setRootIsDecorated(false);
  listView_->setAlternatingRowColors(true);
  listView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  listView_->setSelectionBehavior(QAbstractItemView::SelectRows);
  listView_->setContextMenuPolicy(Qt::CustomContextMenu);
  listView_->setSortingEnabled(true);
  listView_->header()->setStretchLastSection(true);

  // Icon grid
  iconView_ = new QListView(stack_);
  iconView_->setModel(proxy_);
  iconView_->setViewMode(QListView::IconMode);
  iconView_->setResizeMode(QListView::Adjust);
  iconView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  iconView_->setContextMenuPolicy(Qt::CustomContextMenu);

  // Compact list
  compactView_ = new QListView(stack_);
  compactView_->setModel(proxy_);
  compactView_->setViewMode(QListView::ListMode);
  compactView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  compactView_->setContextMenuPolicy(Qt::CustomContextMenu);

  stack_->addWidget(listView_);
  stack_->addWidget(iconView_);
  stack_->addWidget(compactView_);

  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->addWidget(stack_);

  // Signals
  connect(listView_, &QTreeView::activated, this, &TrashView::onActivated);
  connect(iconView_, &QListView::activated, this, &TrashView::onActivated);
  connect(compactView_, &QListView::activated, this, &TrashView::onActivated);

  connect(listView_, &QWidget::customContextMenuRequested, this, &TrashView::onContextMenu);
  connect(iconView_, &QWidget::customContextMenuRequested, this, &TrashView::onContextMenu);
  connect(compactView_, &QWidget::customContextMenuRequested, this, &TrashView::onContextMenu);

  // Inline status bar updates (safe + slightly redundant)
  auto hookSelection = [this](QAbstractItemView *v) {
    if (!v || !v->selectionModel()) return;
    connect(v->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]{
      emit selectionChanged();
    });
  };
  hookSelection(listView_);
  hookSelection(iconView_);
  hookSelection(compactView_);

  connect(proxy_, &QAbstractItemModel::modelReset, this, [this]{ emit itemCountChanged(); });
  connect(proxy_, &QAbstractItemModel::layoutChanged, this, [this]{ emit itemCountChanged(); });
  connect(proxy_, &QAbstractItemModel::rowsInserted, this, [this]{ emit itemCountChanged(); });
  connect(proxy_, &QAbstractItemModel::rowsRemoved, this, [this]{ emit itemCountChanged(); });

  setViewMode(ViewMode::List);
  setSort(TrashModel::Name, Qt::AscendingOrder);
}

int TrashView::itemCount() const {
  return proxy_ ? proxy_->rowCount() : 0;
}

void TrashView::refresh() {
  model_->refresh();
  proxy_->invalidate();

  emit itemCountChanged();
  emit selectionChanged();

  if (model_->rowCount() > 1)
    proxy_->sort(sortColumn_, sortOrder_);
}

void TrashView::setViewMode(ViewMode m) {
  viewMode_ = m;
  switch (m) {
    case ViewMode::List: stack_->setCurrentWidget(listView_); break;
    case ViewMode::GridIcons: stack_->setCurrentWidget(iconView_); break;
    case ViewMode::Compact: stack_->setCurrentWidget(compactView_); break;
  }
}

void TrashView::setSort(int column, Qt::SortOrder order) {
  sortColumn_ = column;
  sortOrder_ = order;
  proxy_->sort(column, order);
}

void TrashView::setFoldersFirst(bool on) {
  foldersFirst_ = on;
  auto *p = dynamic_cast<TrashSortProxy*>(proxy_);
  if (p) p->setFoldersFirst(on);
  proxy_->sort(sortColumn_, sortOrder_);
}

QAbstractItemView* TrashView::currentView() const {
  QWidget *w = stack_->currentWidget();
  return qobject_cast<QAbstractItemView*>(w);
}

QModelIndexList TrashView::selectedRows() const {
  auto *v = currentView();
  if (!v || !v->selectionModel()) return {};
  // Always use rows on column 0.
  return v->selectionModel()->selectedRows(0);
}

QStringList TrashView::selectedTrashedPaths() const {
  QStringList out;
  for (const QModelIndex &pidx : selectedRows()) {
    const QModelIndex src = proxy_->mapToSource(pidx);
    const QString trashed = model_->data(src, TrashModel::TrashedPathRole).toString();
    if (!trashed.isEmpty()) out << trashed;
  }
  return out;
}

bool TrashView::deleteSelectedPermanently(QString *errorOut) {
  const auto rows = selectedRows();
  if (rows.isEmpty()) return true;

  for (const QModelIndex &pidx : rows) {
    const QModelIndex src = proxy_->mapToSource(pidx);
    const QString trashed = model_->data(src, TrashModel::TrashedPathRole).toString();
    const QString info = model_->data(src, TrashModel::InfoPathRole).toString();

    QString err;
    if (!removePathRecursively(trashed, &err)) {
      if (errorOut) *errorOut = err;
      return false;
    }
    if (!info.isEmpty()) QFile::remove(info);
  }

  refresh();
  return true;
}

bool TrashView::restoreSelected(QString *errorOut) {
  const auto rows = selectedRows();
  if (rows.isEmpty()) return true;

  for (const QModelIndex &pidx : rows) {
    const QModelIndex src = proxy_->mapToSource(pidx);
    const QString trashed = model_->data(src, TrashModel::TrashedPathRole).toString();
    const QString info = model_->data(src, TrashModel::InfoPathRole).toString();
    const QString orig = model_->data(src, TrashModel::OriginalPathRole).toString();

    if (trashed.isEmpty() || orig.isEmpty()) {
      if (errorOut) *errorOut = "Missing trash metadata.";
      return false;
    }

    const QFileInfo origInfo(orig);
    QDir parentDir = origInfo.dir();
    if (!parentDir.exists()) {
      if (!QDir().mkpath(parentDir.absolutePath())) {
        if (errorOut) *errorOut = "Could not create parent folder:\n" + parentDir.absolutePath();
        return false;
      }
    }

    if (QFileInfo::exists(orig)) {
      if (errorOut) *errorOut = "Restore destination already exists:\n" + orig;
      return false;
    }

    if (!QFile::rename(trashed, orig)) {
      if (errorOut) *errorOut = "Failed to restore:\n" + orig;
      return false;
    }

    if (!info.isEmpty()) QFile::remove(info);
  }

  refresh();
  return true;
}

void TrashView::onActivated(const QModelIndex &pidx) {
  if (!pidx.isValid()) return;
  const QModelIndex src = proxy_->mapToSource(pidx.sibling(pidx.row(), 0));

  const QString trashed = model_->data(src, TrashModel::TrashedPathRole).toString();
  const bool isDir = model_->data(src, TrashModel::IsDirRole).toBool();

  if (trashed.isEmpty()) return;

  if (isDir) {
    emit requestNavigate(trashed);
    return;
  }

  QDesktopServices::openUrl(QUrl::fromLocalFile(trashed));
}

void TrashView::onContextMenu(const QPoint &pos) {
  auto *v = currentView();
  if (!v) return;

  const QModelIndex idx = v->indexAt(pos);
  if (idx.isValid()) {
    v->setCurrentIndex(idx);
  }

  QMenu menu(this);

  QAction *actOpen = menu.addAction("Open");
  QAction *actRestore = menu.addAction("Restore");
  QAction *actDelete = menu.addAction("Delete Permanently");
  menu.addSeparator();
  QAction *actRefresh = menu.addAction("Refresh");

  QAction *chosen = menu.exec(v->viewport()->mapToGlobal(pos));
  if (!chosen) return;

  if (chosen == actOpen) {
    onActivated(v->currentIndex());
    return;
  }

  if (chosen == actRestore) {
    QString err;
    if (!restoreSelected(&err)) QMessageBox::warning(this, "Restore failed", err);
    return;
  }

  if (chosen == actDelete) {
    const auto sel = selectedTrashedPaths();
    if (sel.isEmpty()) return;
    const auto resp = QMessageBox::warning(
        this, "Delete from Trash",
        "Permanently delete the selected item(s) from Trash?\nThis cannot be undone.\n\n"
        "Delete " + QString::number(sel.size()) + " item(s)?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (resp != QMessageBox::Yes) return;

    QString err;
    if (!deleteSelectedPermanently(&err)) QMessageBox::warning(this, "Delete failed", err);
    return;
  }

  if (chosen == actRefresh) {
    refresh();
    return;
  }
}
