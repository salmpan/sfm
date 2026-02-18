#pragma once

#include <QWidget>
#include <QString>

#include <QVector>

class QHBoxLayout;
class QToolButton;

class BreadcrumbBar final : public QWidget {
  Q_OBJECT
public:
  explicit BreadcrumbBar(QWidget *parent = nullptr);

  // Accepts absolute filesystem paths ("/home/...") or "trash:///".
  void setLocation(const QString &loc);
  QString location() const { return loc_; }

signals:
  void pathActivated(const QString &path);
  void openInNewTabRequested(const QString &path);
  void requestEdit();

protected:
  void mousePressEvent(QMouseEvent *e) override;
  void resizeEvent(QResizeEvent *e) override;
  void keyPressEvent(QKeyEvent *e) override;
  void focusInEvent(QFocusEvent *e) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  struct Seg { QString label; QString full; QString parent; };

  QList<Seg> parseSegments_(const QString &loc) const;
  QList<int> computeVisibleIndices_(const QList<Seg> &segs, int availablePx) const;
  void rebuild_();
  void clearLayout_();
  void focusLast_();

  QString loc_;
  QHBoxLayout *layout_{nullptr};
  QVector<QToolButton*> buttons_;
};
