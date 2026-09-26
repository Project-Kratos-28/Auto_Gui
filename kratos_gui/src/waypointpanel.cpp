#include "waypointpanel.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSet>
#include <QVBoxLayout>

#include <cmath>

// waypoint_manager lists not-yet-bound tags as "<name> (pending)";
// every other service only knows the bare name.
static const QString kPendingSuffix = " (pending)";
static const int kPendingRole = Qt::UserRole;
static const int kNameRole = Qt::UserRole + 1; // row text also shows state

static QString itemName(const QListWidgetItem *item) {
  return item->data(kNameRole).toString();
}

static QString bareName(const QString &listed) {
  return listed.endsWith(kPendingSuffix)
             ? listed.left(listed.size() - kPendingSuffix.size())
             : listed;
}

WaypointPanel::WaypointPanel(RosWorker *rosWorker, QWidget *parent)
    : QWidget(parent), rosWorker_(rosWorker) {
  setStyleSheet(R"(
    QLabel#panelTitle { color: #FFFF55; font-size: 12px; font-weight: bold; }
    QLabel { color: #AAAAAA; font-size: 11px; }
    QLineEdit {
        background-color: #1C1C1C; color: #FFFFFF; border: 1px solid #333333;
        padding: 5px; font-size: 12px;
    }
    QLineEdit:focus { border: 1px solid #FFFF55; }
    QPushButton {
        color: #CCCCCC; background-color: #1C1C1C; border: 1px solid #333333;
        padding: 6px 10px; font-size: 11px;
    }
    QPushButton:hover { border: 1px solid #FFFF55; color: #FFFFFF; }
    QPushButton:disabled { color: #555555; border: 1px solid #252525; }
    QPushButton#logBtn { color: #000000; background-color: #FFFF55; font-weight: bold; }
    QPushButton#logBtn:hover { background-color: #FFFF99; }
    QPushButton#logBtn:disabled { background-color: #333333; color: #666666; }
    QListWidget {
        background-color: #0A0A0A; color: #CCCCCC; border: 1px solid #333333;
        font-size: 12px;
    }
    QListWidget::item { padding: 4px; }
    QListWidget::item:selected { background-color: #252525; color: #FFFF55; }
  )");

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);

  QHBoxLayout *header = new QHBoxLayout();
  QLabel *title = new QLabel("WAYPOINTS", this);
  title->setObjectName("panelTitle");
  serviceLabel_ = new QLabel(this);
  header->addWidget(title);
  header->addStretch();
  header->addWidget(serviceLabel_);
  layout->addLayout(header);

  QHBoxLayout *logRow = new QHBoxLayout();
  nameEdit_ = new QLineEdit(this);
  // Names end up inside a ROS parameter list when a mission starts
  nameEdit_->setValidator(new QRegularExpressionValidator(
      QRegularExpression("[A-Za-z0-9_-]{1,32}"), nameEdit_));
  connect(nameEdit_, &QLineEdit::returnPressed, this,
          &WaypointPanel::logWaypoint);
  logBtn_ = new QPushButton("LOG WAYPOINT", this);
  logBtn_->setObjectName("logBtn");
  logBtn_->setToolTip("Tag the rover's current pose (Ctrl+L)");
  connect(logBtn_, &QPushButton::clicked, this, &WaypointPanel::logWaypoint);
  logRow->addWidget(nameEdit_, 1);
  logRow->addWidget(logBtn_);
  layout->addLayout(logRow);

  statusLabel_ = new QLabel(this);
  statusLabel_->setWordWrap(true);
  layout->addWidget(statusLabel_);

  list_ = new QListWidget(this);
  list_->setDragDropMode(QAbstractItemView::InternalMove);
  list_->setToolTip("Ticked waypoints are visited top to bottom. Drag to "
                    "reorder. Click one to see where GLIM has it now.");
  connect(list_, &QListWidget::itemClicked, this,
          &WaypointPanel::onItemClicked);
  layout->addWidget(list_, 1);

  poseLabel_ = new QLabel("Click a waypoint to see its current pose", this);
  poseLabel_->setWordWrap(true);
  layout->addWidget(poseLabel_);

  saveBtn_ = new QPushButton("SAVE TO YAML…", this);
  connect(saveBtn_, &QPushButton::clicked, this,
          &WaypointPanel::onSaveClicked);
  layout->addWidget(saveBtn_);

  connect(rosWorker_, &RosWorker::waypointServiceStatusChanged, this,
          &WaypointPanel::onServiceStatusChanged);
  connect(rosWorker_, &RosWorker::waypointsListed, this,
          &WaypointPanel::onWaypointsListed);
  connect(rosWorker_, &RosWorker::waypointAdded, this,
          &WaypointPanel::onWaypointAdded);
  connect(rosWorker_, &RosWorker::waypointResolved, this,
          &WaypointPanel::onWaypointResolved);
  connect(rosWorker_, &RosWorker::waypointsSaved, this,
          &WaypointPanel::onWaypointsSaved);

  onServiceStatusChanged(false);
}

QStringList WaypointPanel::missionWaypoints() const {
  QStringList names;
  for (int i = 0; i < list_->count(); ++i) {
    if (list_->item(i)->checkState() == Qt::Checked) {
      names.append(itemName(list_->item(i)));
    }
  }
  return names;
}

void WaypointPanel::logWaypoint() {
  QString name = nameEdit_->text().trimmed();
  if (name.isEmpty()) {
    name = nextAutoName();
  }
  if (findItem(name)) {
    showStatus(QString("'%1' already exists. Pick another name.").arg(name),
               false);
    return;
  }
  logBtn_->setEnabled(false);
  showStatus(QString("Tagging '%1'…").arg(name), true);
  QMetaObject::invokeMethod(rosWorker_, "callAddWaypoint",
                            Qt::QueuedConnection, Q_ARG(QString, name));
}

void WaypointPanel::onServiceStatusChanged(bool online) {
  serviceLabel_->setText(online ? "GLIM: ONLINE" : "GLIM: OFFLINE");
  serviceLabel_->setStyleSheet(online ? "color: #55FF55; font-weight: bold;"
                                      : "color: #FF5555; font-weight: bold;");
  logBtn_->setEnabled(online);
  saveBtn_->setEnabled(online);
  nameEdit_->setPlaceholderText(online ? nextAutoName()
                                       : "start GLIM to tag waypoints");
}

void WaypointPanel::onWaypointsListed(QStringList names) {
  // Merge instead of rebuilding, so ticks and the user's order survive the
  // 1 s refresh. New names go to the bottom, ticked.
  QSet<QString> present;
  for (const QString &listed : names) {
    const QString name = bareName(listed);
    present.insert(name);
    QListWidgetItem *item = findItem(name);
    if (!item) {
      item = new QListWidgetItem(name, list_);
      item->setData(kNameRole, name);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable |
                     Qt::ItemIsDragEnabled);
      item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
      item->setCheckState(Qt::Checked);
    }
    setItemPending(item, listed.endsWith(kPendingSuffix));
  }
  for (int i = list_->count() - 1; i >= 0; --i) {
    if (!present.contains(itemName(list_->item(i)))) {
      delete list_->takeItem(i);
    }
  }
  if (nameEdit_->text().isEmpty()) {
    nameEdit_->setPlaceholderText(nextAutoName());
  }
}

void WaypointPanel::onWaypointAdded(bool success, QString name,
                                    QString message) {
  logBtn_->setEnabled(true);
  if (success) {
    nameEdit_->clear();
    showStatus(QString("Tagged '%1'. %2").arg(name, message), true);
  } else {
    showStatus(QString("Could not tag '%1': %2").arg(name, message), false);
  }
}

void WaypointPanel::onWaypointResolved(QString name, bool found, double x,
                                       double y, double z, double yaw) {
  QListWidgetItem *item = findItem(name);
  if (!found) {
    const bool pending = item && item->data(kPendingRole).toBool();
    poseLabel_->setText(
        pending ? QString("%1: pending. Keep driving so GLIM closes its "
                          "submap.")
                      .arg(name)
                : QString("%1: not found in GLIM").arg(name));
    return;
  }
  poseLabel_->setText(QString("%1: x %2  y %3  z %4  yaw %5°  (map)")
                          .arg(name)
                          .arg(x, 0, 'f', 2)
                          .arg(y, 0, 'f', 2)
                          .arg(z, 0, 'f', 2)
                          .arg(yaw * 180.0 / M_PI, 0, 'f', 0));
}

void WaypointPanel::onWaypointsSaved(bool success, QString message) {
  showStatus(success ? "Saved: " + message : "Save failed: " + message,
             success);
}

void WaypointPanel::onSaveClicked() {
  bool ok = false;
  const QString defaultPath =
      QString("/tmp/kratos_waypoints_%1.yaml")
          .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
  const QString path = QInputDialog::getText(
      this, "Save waypoints",
      "YAML path on the machine running GLIM:", QLineEdit::Normal,
      defaultPath, &ok);
  if (!ok || path.trimmed().isEmpty()) {
    return;
  }
  QMetaObject::invokeMethod(rosWorker_, "callSaveWaypoints",
                            Qt::QueuedConnection,
                            Q_ARG(QString, path.trimmed()));
}

void WaypointPanel::onItemClicked(QListWidgetItem *item) {
  poseLabel_->setText(QString("%1: asking GLIM…").arg(itemName(item)));
  QMetaObject::invokeMethod(rosWorker_, "callGetWaypoint",
                            Qt::QueuedConnection,
                            Q_ARG(QString, itemName(item)));
}

QString WaypointPanel::nextAutoName() const {
  static const QRegularExpression re("^wp(\\d+)$");
  int highest = 0;
  for (int i = 0; i < list_->count(); ++i) {
    const auto m = re.match(itemName(list_->item(i)));
    if (m.hasMatch()) {
      highest = std::max(highest, m.captured(1).toInt());
    }
  }
  return QString("wp%1").arg(highest + 1);
}

QListWidgetItem *WaypointPanel::findItem(const QString &name) const {
  for (int i = 0; i < list_->count(); ++i) {
    if (itemName(list_->item(i)) == name) {
      return list_->item(i);
    }
  }
  return nullptr;
}

void WaypointPanel::setItemPending(QListWidgetItem *item, bool pending) {
  if (item->data(kPendingRole).isValid() &&
      item->data(kPendingRole).toBool() == pending) {
    return;
  }
  item->setData(kPendingRole, pending);
  item->setText(pending ? itemName(item) + "   · pending" : itemName(item));
  item->setForeground(pending ? QColor("#FFAA33") : QColor("#CCCCCC"));
  item->setToolTip(pending ? "Pending: its GLIM submap isn't finalized yet, "
                             "so it can't be resolved or loop-closure "
                             "corrected. Keep driving."
                           : "Bound to a GLIM submap");
}

void WaypointPanel::showStatus(const QString &text, bool ok) {
  statusLabel_->setText(text);
  statusLabel_->setStyleSheet(ok ? "color: #AAAAAA;" : "color: #FF5555;");
}
