#include "breadcrumbbar.h"

#include <QHBoxLayout>
#include <QToolButton>
#include <QLabel>
#include <QMenu>
#include <QDir>
#include <QFileInfo>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QApplication>
#include <QClipboard>
#include <QEvent>

#include <climits>

static void addSeparator(QHBoxLayout *layout) {
  auto *sep = new QLabel(">", nullptr);
  sep->setContentsMargins(6, 0, 6, 0);
  sep->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
  layout->addWidget(sep);
}

BreadcrumbBar::BreadcrumbBar(QWidget *parent)
  : QWidget(parent) {
  layout_ = new QHBoxLayout(this);
  layout_->setContentsMargins(0, 0, 0, 0);
  layout_->setSpacing(0);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  setFocusPolicy(Qt::StrongFocus);
}

void BreadcrumbBar::setLocation(const QString &loc) {
  if (loc_ == loc) return;
  loc_ = loc;
  rebuild_();
}

void BreadcrumbBar::mousePressEvent(QMouseEvent *e) {
  if (e->button() == Qt::LeftButton) {
    emit requestEdit();
    e->accept();
    return;
  }
  QWidget::mousePressEvent(e);
}

void BreadcrumbBar::resizeEvent(QResizeEvent *e) {
  QWidget::resizeEvent(e);
  // Reflow (collapse/expand) on resize.
  rebuild_();
}

void BreadcrumbBar::focusInEvent(QFocusEvent *e) {
  QWidget::focusInEvent(e);
  focusLast_();
}

void BreadcrumbBar::keyPressEvent(QKeyEvent *e) {
  if (buttons_.isEmpty()) {
    QWidget::keyPressEvent(e);
    return;
  }

  const int n = static_cast<int>(buttons_.size());

  const int cur = [&]() -> int {
    for (int i = 0; i < n; ++i) {
      if (buttons_[i] && buttons_[i]->hasFocus()) return i;
    }
    return n - 1;
  }();

  auto focusIndex = [&](int idx) {
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    if (buttons_[idx]) buttons_[idx]->setFocus(Qt::ShortcutFocusReason);
  };

  switch (e->key()) {
    case Qt::Key_Left:
      focusIndex(cur - 1);
      e->accept();
      return;
    case Qt::Key_Right:
      focusIndex(cur + 1);
      e->accept();
      return;
    case Qt::Key_Home:
      focusIndex(0);
      e->accept();
      return;
    case Qt::Key_End:
      focusIndex(n - 1);
      e->accept();
      return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
      if (buttons_[cur]) buttons_[cur]->click();
      e->accept();
      return;
    case Qt::Key_Down:
      if (buttons_[cur] && buttons_[cur]->menu()) {
        buttons_[cur]->showMenu();
        e->accept();
        return;
      }
      break;
    default:
      break;
  }

  QWidget::keyPressEvent(e);
}

bool BreadcrumbBar::eventFilter(QObject *watched, QEvent *event) {
  // Allow navigating between crumb buttons while a button has focus.
  if (event->type() == QEvent::KeyPress) {
    auto *ke = static_cast<QKeyEvent*>(event);
    // Send the key to the bar handler; it will act on the currently focused button.
    QApplication::sendEvent(this, ke);
    if (ke->isAccepted()) return true;
  }
  return QWidget::eventFilter(watched, event);
}

void BreadcrumbBar::clearLayout_() {
  while (auto *item = layout_->takeAt(0)) {
    if (item->widget()) item->widget()->deleteLater();
    delete item;
  }
}

static QMenu* buildSiblingsMenu(const QString &parentDir, const QString &currentFull, QWidget *parentWidget) {
  QDir d(parentDir);
  if (!d.exists()) return nullptr;

  auto *m = new QMenu(parentWidget);
  const QFileInfoList infos = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name | QDir::IgnoreCase);
  for (const QFileInfo &fi : infos) {
    QAction *a = m->addAction(fi.fileName());
    a->setData(fi.absoluteFilePath());
    if (fi.absoluteFilePath() == currentFull) {
      a->setCheckable(true);
      a->setChecked(true);
    }
  }
  if (m->actions().isEmpty()) {
    delete m;
    return nullptr;
  }
  return m;
}

static QMenu* buildContextMenu(const QString &label, const QString &fullPath, QWidget *parentWidget) {
  auto *m = new QMenu(parentWidget);

  QAction *copyPath = m->addAction("Copy path");
  copyPath->setData(fullPath);

  QAction *copyName = m->addAction("Copy name");
  copyName->setData(label);

  m->addSeparator();
  QAction *openNewTab = m->addAction("Open in new tab");
  openNewTab->setData(fullPath);

  return m;
}

QList<BreadcrumbBar::Seg> BreadcrumbBar::parseSegments_(const QString &loc) const {
  QList<Seg> segs;

  if (loc.startsWith("trash://")) {
    segs.push_back({"Trash", "trash:///", QString()});
    return segs;
  }

  QString path = loc;
  if (path.isEmpty()) path = "/";
  if (!path.startsWith('/')) {
    segs.push_back({path, path, QString()});
    return segs;
  }

  path = QDir::cleanPath(path);
  const QString home = QDir::homePath();
  const bool inHome = (path == home) || path.startsWith(home + "/");

  if (inHome) {
    QFileInfo hi(home);
    segs.push_back({"Home", home, hi.absolutePath()});
    const QString rel = (path == home) ? QString() : path.mid(home.size() + 1);
    const QStringList parts = rel.isEmpty() ? QStringList() : rel.split('/', Qt::SkipEmptyParts);
    QString acc = home;
    for (const QString &p : parts) {
      const QString parent = acc;
      acc = acc + "/" + p;
      segs.push_back({p, acc, parent});
    }
  } else {
    segs.push_back({"/", "/", "/"});
    const QString rel = (path == "/") ? QString() : path.mid(1);
    const QStringList parts = rel.isEmpty() ? QStringList() : rel.split('/', Qt::SkipEmptyParts);
    QString acc;
    for (const QString &p : parts) {
      const QString parent = acc.isEmpty() ? "/" : ("/" + acc);
      acc = acc.isEmpty() ? p : (acc + "/" + p);
      const QString full = "/" + acc;
      segs.push_back({p, full, parent});
    }
  }

  return segs;
}

static int estimateSeparatorPx(const QFontMetrics &fm) {
  return fm.horizontalAdvance(">") + 12;
}

static int estimateButtonPx(const QFontMetrics &fm, const QString &text) {
  // Approximate toolbutton padding/arrow area.
  return fm.horizontalAdvance(text) + 34;
}

QList<int> BreadcrumbBar::computeVisibleIndices_(const QList<Seg> &segs, int availablePx) const {
  QList<int> all;
  for (int i = 0; i < segs.size(); ++i) all.push_back(i);
  if (segs.size() <= 4) return all;

  const QFontMetrics fm(font());
  const int sepPx = estimateSeparatorPx(fm);

  auto estimatePlan = [&](int head, int tail) -> int {
    if (head + tail >= segs.size()) {
      int px = 0;
      for (int i = 0; i < segs.size(); ++i) {
        if (i) px += sepPx;
        px += estimateButtonPx(fm, segs[i].label);
      }
      return px;
    }

    int px = 0;
    // head
    for (int i = 0; i < head; ++i) {
      if (i) px += sepPx;
      px += estimateButtonPx(fm, segs[i].label);
    }
    // ellipsis separator + button
    if (head) px += sepPx;
    px += estimateButtonPx(fm, "…");

    // tail
    for (int j = segs.size() - tail; j < segs.size(); ++j) {
      px += sepPx;
      px += estimateButtonPx(fm, segs[j].label);
    }
    return px;
  };

  // Search for the best (most segments shown) plan that fits.
  struct Plan { int head{0}; int tail{0}; int shown{0}; int px{0}; } best;
  best.px = INT_MAX;

  const int maxHead = qMin(3, segs.size());
  const int maxTail = qMin(5, segs.size());
  for (int head = 1; head <= maxHead; ++head) {
    for (int tail = 1; tail <= maxTail; ++tail) {
      if (head + tail > segs.size()) continue;
      const int px = estimatePlan(head, tail);
      const int shown = (head + tail >= segs.size()) ? segs.size() : (head + 1 + tail); // +ellipsis
      if (px <= availablePx) {
        if (shown > best.shown || (shown == best.shown && px < best.px)) {
          best = {head, tail, shown, px};
        }
      }
    }
  }

  if (best.shown == 0) {
    // Nothing fits by estimate: show first + ellipsis + last.
    return {0, -1, static_cast<int>(segs.size()) - 1};
  }

  if (best.head + best.tail >= segs.size()) return all;

  QList<int> idx;
  for (int i = 0; i < best.head; ++i) idx.push_back(i);
  idx.push_back(-1); // ellipsis marker
  for (int j = segs.size() - best.tail; j < segs.size(); ++j) idx.push_back(j);
  return idx;
}

void BreadcrumbBar::rebuild_() {
  clearLayout_();
  buttons_.clear();

  const QList<Seg> segs = parseSegments_(loc_);
  if (segs.isEmpty()) {
    layout_->addStretch(1);
    return;
  }

  const int available = qMax(0, width() - 8);
  const QList<int> visible = computeVisibleIndices_(segs, available);

  // If we collapsed, build a menu for hidden segments.
  QMenu *hiddenMenu = nullptr;
  if (visible.contains(-1)) {
    hiddenMenu = new QMenu(this);
    for (int i = 0; i < segs.size(); ++i) {
      if (visible.contains(i) || i == 0 || i == segs.size() - 1) continue;
      QAction *a = hiddenMenu->addAction(segs[i].label);
      a->setData(segs[i].full);
    }
    if (hiddenMenu->actions().isEmpty()) {
      delete hiddenMenu;
      hiddenMenu = nullptr;
    }
  }

  bool first = true;
  for (int vi = 0; vi < visible.size(); ++vi) {
    const int idx = visible[vi];
    if (!first) addSeparator(layout_);
    first = false;

    if (idx == -1) {
      auto *btn = new QToolButton(this);
      btn->setText("…");
      btn->setAutoRaise(true);
      btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
      btn->setPopupMode(QToolButton::InstantPopup);
      btn->setFocusPolicy(Qt::StrongFocus);
      if (hiddenMenu) {
        btn->setMenu(hiddenMenu);
        connect(hiddenMenu, &QMenu::triggered, this, [this](QAction *a){
          const QString p = a->data().toString();
          if (!p.isEmpty()) emit pathActivated(p);
        });
      }

      // Context menu on ellipsis.
      btn->setContextMenuPolicy(Qt::CustomContextMenu);
      connect(btn, &QToolButton::customContextMenuRequested, this, [this, btn]{
        auto *m = new QMenu(btn);
        QAction *edit = m->addAction("Edit path");
        QAction *chosen = m->exec(btn->mapToGlobal(QPoint(0, btn->height())));
        if (chosen == edit) emit requestEdit();
        m->deleteLater();
      });

      layout_->addWidget(btn);
      buttons_.push_back(btn);
      btn->installEventFilter(this);
      continue;
    }

    const auto &s = segs[idx];

    auto *btn = new QToolButton(this);
    btn->setText(s.label);
    btn->setAutoRaise(true);
    btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    btn->setPopupMode(QToolButton::MenuButtonPopup);
    btn->setFocusPolicy(Qt::StrongFocus);

    connect(btn, &QToolButton::clicked, this, [this, s]{ emit pathActivated(s.full); });

    // Siblings dropdown
    if (!s.parent.isEmpty()) {
      QMenu *menu = buildSiblingsMenu(s.parent, s.full, btn);
      if (menu) {
        btn->setMenu(menu);
        connect(menu, &QMenu::triggered, this, [this](QAction *a){
          const QString p = a->data().toString();
          if (!p.isEmpty()) emit pathActivated(p);
        });
      } else {
        btn->setPopupMode(QToolButton::DelayedPopup);
      }
    }

    // Right-click context menu
    btn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(btn, &QToolButton::customContextMenuRequested, this, [this, s, btn](const QPoint &){
      QMenu *m = buildContextMenu(s.label, s.full, btn);
      QAction *chosen = m->exec(btn->mapToGlobal(QPoint(0, btn->height())));
      if (!chosen) return;

      const QString data = chosen->data().toString();
      if (chosen->text() == "Copy path") {
        QGuiApplication::clipboard()->setText(data);
      } else if (chosen->text() == "Copy name") {
        QGuiApplication::clipboard()->setText(data);
      } else if (chosen->text() == "Open in new tab") {
        emit openInNewTabRequested(data);
      }
    });

    layout_->addWidget(btn);
    buttons_.push_back(btn);
    btn->installEventFilter(this);
  }

  layout_->addStretch(1);
  focusLast_();
}

void BreadcrumbBar::focusLast_() {
  if (!isVisible()) return;
  if (!hasFocus()) return;
  if (buttons_.isEmpty()) return;
  if (!buttons_.back()) return;
  buttons_.back()->setFocus(Qt::ShortcutFocusReason);
}
