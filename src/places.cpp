#include "places.h"
#include "udisks.h"

#include <QListView>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QVBoxLayout>
#include <QIcon>
#include <QStandardPaths>
#include <QDir>
#include <QApplication>
#include <QMenu>
#include <QMessageBox>
#include <QStyle>

static QIcon themeIcon(const QString &name, const QString &fallback = QString()) {
  QIcon ic = QIcon::fromTheme(name);
  if (!ic.isNull()) return ic;
  if (!fallback.isEmpty()) {
    ic = QIcon::fromTheme(fallback);
    if (!ic.isNull()) return ic;
  }
  return QApplication::style()->standardIcon(QStyle::SP_DirIcon);
}

PlacesSidebar::PlacesSidebar(QWidget *parent) : QWidget(parent) {
  model_ = new QStandardItemModel(this);

  view_ = new QListView(this);
  view_->setModel(model_);
  view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view_->setSelectionMode(QAbstractItemView::SingleSelection);
  view_->setUniformItemSizes(true);
  view_->setContextMenuPolicy(Qt::CustomContextMenu);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(view_);

  connect(view_, &QListView::activated, this, &PlacesSidebar::onActivated);
  connect(view_, &QListView::doubleClicked, this, &PlacesSidebar::onActivated);
  connect(view_, &QWidget::customContextMenuRequested, this, &PlacesSidebar::onContextMenu);

  udisks_ = new UDisks2(this);
  if (udisks_->isAvailable()) {
    connect(udisks_, &UDisks2::devicesChanged, this, [this]{ buildDeviceSection(); });
  }

  buildStaticPlaces();
  buildDeviceSection();
  updateTrashBadge();
}

void PlacesSidebar::refresh() {
  buildStaticPlaces();
  buildDeviceSection();
  updateTrashBadge();
}

void PlacesSidebar::onActivated(const QModelIndex &index) {
  if (!index.isValid()) return;
  const QString path = index.data(RolePath).toString();
  if (path.isEmpty()) return;

  const Kind kind = (Kind)index.data(RoleKind).toInt();
  if (kind == Kind::Device) {
    const QString blockObj = index.data(RoleUDisksBlockPath).toString();
    const bool mounted = index.data(RoleIsMounted).toBool();

    if (mounted) {
      emit placeActivated(path);
      return;
    }

    if (!udisks_ || !udisks_->isAvailable()) return;
    QString mountedAt, err;
    if (!udisks_->mount(blockObj, &mountedAt, &err)) {
      QMessageBox::warning(this, "Mount failed", err);
      return;
    }
    emit placeActivated(mountedAt);
    return;
  }

  emit placeActivated(path);
}

void PlacesSidebar::onContextMenu(const QPoint &pos) {
  const QModelIndex idx = view_->indexAt(pos);
  if (!idx.isValid()) return;

  const QString path = idx.data(RolePath).toString();
  if (path.isEmpty()) return;

  const Kind kind = (Kind)idx.data(RoleKind).toInt();

  QMenu menu(this);
  QAction *aOpen = menu.addAction("Open");

  QAction *aEmptyTrash = nullptr;
  QAction *aMount = nullptr;
  QAction *aUnmount = nullptr;
  QAction *aEject = nullptr;

  if (kind == Kind::Trash) {
    menu.addSeparator();
    aEmptyTrash = menu.addAction("Empty Trash");
  }

  if (kind == Kind::Device) {
    menu.addSeparator();

    const bool mounted = idx.data(RoleIsMounted).toBool();
    const bool canEject = idx.data(RoleCanEject).toBool();

    aMount = menu.addAction("Mount");
    aUnmount = menu.addAction("Unmount");
    aEject = menu.addAction("Eject");

    aMount->setEnabled(!mounted);
    aUnmount->setEnabled(mounted);
    aEject->setEnabled(canEject);
  }

  QAction *chosen = menu.exec(view_->viewport()->mapToGlobal(pos));
  if (!chosen) return;

  if (chosen == aOpen) {
    onActivated(idx);
    return;
  }

  if (aEmptyTrash && chosen == aEmptyTrash) {
    emit emptyTrashRequested();
    return;
  }

  if (kind == Kind::Device && udisks_ && udisks_->isAvailable()) {
    const QString blockObj = idx.data(RoleUDisksBlockPath).toString();
    const QString driveObj = idx.data(RoleUDisksDrivePath).toString();

    if (aMount && chosen == aMount) {
      QString mountedAt, err;
      if (!udisks_->mount(blockObj, &mountedAt, &err)) QMessageBox::warning(this, "Mount failed", err);
      else emit placeActivated(mountedAt);
      buildDeviceSection();
      return;
    }
    if (aUnmount && chosen == aUnmount) {
      QString err;
      if (!udisks_->unmount(blockObj, &err)) QMessageBox::warning(this, "Unmount failed", err);
      buildDeviceSection();
      return;
    }
    if (aEject && chosen == aEject) {
      QString err;
      if (!udisks_->ejectDrive(driveObj, &err)) QMessageBox::warning(this, "Eject failed", err);
      buildDeviceSection();
      return;
    }
  }
}

void PlacesSidebar::addSectionHeader(const QString &title, Section section) {
  auto *hdr = new QStandardItem(title);
  hdr->setSelectable(false);
  hdr->setEnabled(false);
  hdr->setData(QString(), RolePath);
  hdr->setData((int)section, RoleSection);
  hdr->setData((int)Kind::Normal, RoleKind);
  model_->appendRow(hdr);
}

QStandardItem* PlacesSidebar::addPlaceItem(const QString &label,
                                          const QString &path,
                                          const QString &iconName,
                                          Section section,
                                          Kind kind) {
  auto *it = new QStandardItem(themeIcon(iconName), label);
  it->setData(path, RolePath);
  it->setData((int)section, RoleSection);
  it->setData((int)kind, RoleKind);
  it->setToolTip(path);
  model_->appendRow(it);
  return it;
}

QString PlacesSidebar::xdgDataHome() {
  const QString env = qEnvironmentVariable("XDG_DATA_HOME");
  if (!env.isEmpty()) return env;
  return QDir::homePath() + "/.local/share";
}

int PlacesSidebar::trashCount() {
  QDir infoDir(xdgDataHome() + "/Trash/info");
  if (!infoDir.exists()) return 0;
  return infoDir.entryList(QStringList() << "*.trashinfo",
                           QDir::Files | QDir::NoDotAndDotDot).size();
}

void PlacesSidebar::updateTrashBadge() {
  if (!trashItem_) return;
  const int n = trashCount();
  const QString base = "Trash";
  trashItem_->setText(n > 0 ? (base + " (" + QString::number(n) + ")") : base);
}

void PlacesSidebar::buildStaticPlaces() {
  model_->clear();
  trashItem_ = nullptr;

  addSectionHeader("Places", Section::Places);

  addPlaceItem("Home", QDir::homePath(), "user-home", Section::Places, Kind::Normal);

  const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
  if (!desktop.isEmpty()) addPlaceItem("Desktop", desktop, "user-desktop", Section::Places, Kind::Normal);

  const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  if (!docs.isEmpty()) addPlaceItem("Documents", docs, "folder-documents", Section::Places, Kind::Normal);

  const QString dl = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  if (!dl.isEmpty()) addPlaceItem("Downloads", dl, "folder-download", Section::Places, Kind::Normal);

  const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
  if (!music.isEmpty()) addPlaceItem("Music", music, "folder-music", Section::Places, Kind::Normal);

  const QString pics = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  if (!pics.isEmpty()) addPlaceItem("Pictures", pics, "folder-pictures", Section::Places, Kind::Normal);

  const QString vids = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
  if (!vids.isEmpty()) addPlaceItem("Videos", vids, "folder-videos", Section::Places, Kind::Normal);

  addPlaceItem("File System", "/", "drive-harddisk", Section::Places, Kind::Normal);

  trashItem_ = addPlaceItem("Trash", "trash:///", "user-trash", Section::Places, Kind::Trash);
}

void PlacesSidebar::buildDeviceSection() {
  int devicesHeaderRow = -1;
  for (int r = 0; r < model_->rowCount(); ++r) {
    QStandardItem *it = model_->item(r);
    if (!it) continue;
    if (it->data(RoleSection).toInt() == (int)Section::Devices && it->data(RolePath).toString().isEmpty()) {
      devicesHeaderRow = r;
      break;
    }
  }
  if (devicesHeaderRow >= 0) {
    model_->removeRows(devicesHeaderRow, model_->rowCount() - devicesHeaderRow);
  }

  addSectionHeader("Devices", Section::Devices);

  if (!udisks_ || !udisks_->isAvailable()) return;

  QString err;
  const QList<UDisks2::Device> devs = udisks_->devices(&err);
  Q_UNUSED(err);

  for (const auto &d : devs) {
    const bool mounted = !d.mountPoints.isEmpty();
    const QString shownPath = mounted ? d.mountPoints.first() : QString("(not mounted)");

    auto *row = addPlaceItem(d.displayName, shownPath,
                            mounted ? "drive-harddisk" : "media-removable",
                            Section::Devices, Kind::Device);

    row->setData(d.objectPath, RoleUDisksBlockPath);
    row->setData(d.driveObjectPath, RoleUDisksDrivePath);
    row->setData(mounted, RoleIsMounted);
    row->setData(d.canEject, RoleCanEject);

    QString tip = d.objectPath;
    if (!d.mountPoints.isEmpty()) tip += "\n" + d.mountPoints.join("\n");
    row->setToolTip(tip);

    row->setData(shownPath, RolePath);
  }
}
