#include "camerawidget.h"
#include "ui_camerawidget.h"

#include <QDebug>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QToolButton>
#include <QProcess>

// ─── Layout options ────────────────────────────────────────────────
// Each entry: "label" → {rows, cols}
static const struct {
  const char *label;
  int rows, cols;
} kLayouts[] = {
    {"1 × 1", 1, 1}, {"2 × 1", 2, 1}, {"2 × 2", 2, 2},
    {"3 × 2", 3, 2}, {"3 × 3", 3, 3}, {"4 × 4", 4, 4},
};
static constexpr int kLayoutCount = sizeof(kLayouts) / sizeof(kLayouts[0]);

// ─── Size presets (fixed cell sizes in px) ─────────────────────────
static const struct {
  const char *label;
  int width, height; // 0,0 = auto-fit (stretch)
} kSizes[] = {
    {"Auto-fit", 0, 0},
    {"Small (320×240)", 320, 240},
    {"Medium (640×480)", 640, 480},
    {"Large (960×720)", 960, 720},
    {"XL (1280×720)", 1280, 720},
};
static constexpr int kSizeCount = sizeof(kSizes) / sizeof(kSizes[0]);

// ─── Shared stylesheet for combo boxes ─────────────────────────────
static const char *kComboStyle = R"(
    QComboBox {
        color: #CCCCCC;
        background-color: #252525;
        border: 1px solid #444444;
        border-radius: 3px;
        padding: 3px 8px;
        font-size: 11px;
        min-width: 120px;
    }
    QComboBox:hover { border: 1px solid #FFFF55; }
    QComboBox::drop-down {
        border: none;
        width: 18px;
    }
    QComboBox::down-arrow {
        image: none;
        border-left: 4px solid transparent;
        border-right: 4px solid transparent;
        border-top: 5px solid #AAAAAA;
        margin-right: 5px;
    }
    QComboBox QAbstractItemView {
        background-color: #1C1C1C;
        color: #CCCCCC;
        selection-background-color: #333333;
        selection-color: #FFFF55;
        border: 1px solid #444444;
        font-size: 11px;
    }
)";

// Small combo for per-cell stream picker
static const char *kCellComboStyle = R"(
    QComboBox {
        color: #BBBBBB;
        background-color: rgba(30, 30, 30, 200);
        border: 1px solid #555555;
        border-radius: 2px;
        padding: 2px 6px;
        font-size: 10px;
        min-width: 90px;
        max-width: 200px;
    }
    QComboBox:hover { border: 1px solid #FFFF55; }
    QComboBox::drop-down { border: none; width: 14px; }
    QComboBox::down-arrow {
        image: none;
        border-left: 3px solid transparent;
        border-right: 3px solid transparent;
        border-top: 4px solid #999999;
        margin-right: 4px;
    }
    QComboBox QAbstractItemView {
        background-color: #1C1C1C;
        color: #CCCCCC;
        selection-background-color: #333333;
        selection-color: #FFFF55;
        border: 1px solid #444444;
        font-size: 10px;
    }
)";

// ────────────────────────────────────────────────────────────────────

CameraWidget::CameraWidget(RosWorker *rosWorker, QWidget *parent)
    : QWidget(parent), ui(new Ui::CameraWidget), rosWorker_(rosWorker) {
  ui->setupUi(this);

  // ── Build toolbar combos ─────────────────────────────────────────
  sidebarSplitToggle_ = new QPushButton("☰", this);
  sidebarSplitToggle_->setStyleSheet(
      "QPushButton { color: #AAAAAA; background: transparent; border: none; font-size: 16px; }"
      "QPushButton:hover { color: #FFFF55; }"
  );
  sidebarSplitToggle_->setFixedSize(30, 30);
  connect(sidebarSplitToggle_, &QPushButton::clicked, this, &CameraWidget::onSidebarToggle);

  QLabel *layoutLabel = new QLabel("LAYOUT", this);
  layoutLabel->setStyleSheet(
      "QLabel { color: #888888; font-size: 10px; font-weight: bold; }");

  layoutCombo_ = new QComboBox(this);
  layoutCombo_->setStyleSheet(kComboStyle);
  for (int i = 0; i < kLayoutCount; ++i)
    layoutCombo_->addItem(kLayouts[i].label);
  layoutCombo_->setCurrentIndex(2); // default 2×2

  QLabel *sizeLabel = new QLabel("SIZE", this);
  sizeLabel->setStyleSheet(
      "QLabel { color: #888888; font-size: 10px; font-weight: bold; }");

  sizeCombo_ = new QComboBox(this);
  sizeCombo_->setStyleSheet(kComboStyle);
  for (int i = 0; i < kSizeCount; ++i)
    sizeCombo_->addItem(kSizes[i].label);
  sizeCombo_->setCurrentIndex(0); // default auto-fit

  // Insert into toolbar (before the spacer, which is already at index 0)
  QHBoxLayout *toolbar = ui->toolbarLayout;
  toolbar->insertWidget(0, sidebarSplitToggle_);
  toolbar->insertWidget(1, layoutLabel);
  toolbar->insertWidget(2, layoutCombo_);
  toolbar->insertSpacing(3, 16);
  toolbar->insertWidget(4, sizeLabel);
  toolbar->insertWidget(5, sizeCombo_);

  connect(layoutCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &CameraWidget::onLayoutChanged);
  connect(sizeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &CameraWidget::onSizeChanged);

  // ── Setup Sidebar ────────────────────────────────────────────────
  camerasListLayout_ =
      qobject_cast<QVBoxLayout *>(ui->sidebarScrollContents->layout());

  // Initialize Server UI
  // Removed initialization logic
  
  // Set default splitter sizes (e.g. 250px sidebar, remainder to grid)
  ui->mainSplitter->setSizes(QList<int>() << 250 << 800);

  // ── Build grid area ──────────────────────────────────────────────
  gridContainer_ = new QWidget(this);
  gridContainer_->setStyleSheet(
      "QWidget { background-color: #0A0A0A; }");
  gridLayout_ = new QGridLayout(gridContainer_);
  gridLayout_->setSpacing(4);
  gridLayout_->setContentsMargins(4, 4, 4, 4);

  ui->gridScrollArea->setWidget(gridContainer_);

  // ── ROS connection ───────────────────────────────────────────────
  connect(rosWorker_, &RosWorker::camerasUpdated, this,
          &CameraWidget::onCamerasUpdated);


  // Build initial grid (2×2)
  rebuildGrid(2, 2);

  setupHotkeys();
}

CameraWidget::~CameraWidget() {
  // Stop all active streams
  for (auto &cell : cells_) {
    if (cell.player) {
      cell.player->stopStream();
    }
  }
  delete ui;
}

// ─── Parse layout index to rows/cols ───────────────────────────────

void CameraWidget::parseLayout(int index, int &rows, int &cols) {
  if (index < 0 || index >= kLayoutCount) {
    rows = 2;
    cols = 2;
    return;
  }
  rows = kLayouts[index].rows;
  cols = kLayouts[index].cols;
}

// ─── Rebuild the grid ──────────────────────────────────────────────

void CameraWidget::rebuildGrid(int rows, int cols) {
  int newTotal = rows * cols;
  int oldTotal = cells_.size();

  // 1. Detach existing cells from layout without destroying them
  for (int i = 0; i < oldTotal; ++i) {
    if (cells_[i].frame) {
      gridLayout_->removeWidget(cells_[i].frame);
    }
  }

  // 2. Recreate layout for a clean slate
  delete gridLayout_;
  gridLayout_ = new QGridLayout(gridContainer_);
  gridLayout_->setSpacing(4);
  gridLayout_->setContentsMargins(4, 4, 4, 4);

  // 3. Remove surplus cells if layout shrunk
  if (newTotal < oldTotal) {
    for (int i = oldTotal - 1; i >= newTotal; --i) {
      if (cells_[i].player && !cells_[i].assignedCamera.isEmpty()) {
        cells_[i].player->stopStream();
      }
      if (cells_[i].frame) {
        cells_[i].frame->hide();
        cells_[i].frame->deleteLater();
      }
    }
    cells_.resize(newTotal);
  }

  // Size settings
  int sizeIdx = sizeCombo_->currentIndex();
  bool autoFit = (sizeIdx == 0);
  int cellW = autoFit ? 0 : kSizes[sizeIdx].width;
  int cellH = autoFit ? 0 : kSizes[sizeIdx].height;

  // 4. Create new cells if layout expanded
  if (newTotal > oldTotal) {
    cells_.resize(newTotal);
    for (int i = oldTotal; i < newTotal; ++i) {
      QWidget *cellFrame = new QWidget(gridContainer_);
      cellFrame->setStyleSheet(
          "QWidget { background-color: #121212; border: 1px solid #2A2A2A; }");

      QVBoxLayout *cellLayout = new QVBoxLayout(cellFrame);
      cellLayout->setContentsMargins(0, 0, 0, 0);
      cellLayout->setSpacing(0);

      QComboBox *picker = new QComboBox(cellFrame);
      picker->setStyleSheet(kCellComboStyle);
      picker->addItem("— None —");
      for (const auto &cam : knownCameras_) {
        picker->addItem(cam.name);
      }
      picker->setProperty("cellIndex", i);
      connect(picker, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
              [this, i](int) { onCellStreamChanged(i); });
      cellLayout->addWidget(picker);

      VideoPlayerWidget *player =
          new VideoPlayerWidget("", 0, rosWorker_, cellFrame);
      cellLayout->addWidget(player, 1);

      cells_[i].frame = cellFrame;
      cells_[i].player = player;
      cells_[i].streamPicker = picker;
      cells_[i].assignedCamera = "";
    }
  }

  // 5. Place all cells in the layout and apply stretches
  for (int i = 0; i < newTotal; ++i) {
    int r = i / cols;
    int c = i % cols;

    if (!autoFit) {
      cells_[i].frame->setMinimumSize(0, 0); // Reset minimum
      cells_[i].frame->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
      cells_[i].frame->setFixedSize(cellW, cellH);
    } else {
      cells_[i].frame->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
      cells_[i].frame->setMinimumSize(200, 150);
      cells_[i].frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    gridLayout_->addWidget(cells_[i].frame, r, c);
    cells_[i].frame->show();

    if (autoFit) {
      gridLayout_->setRowStretch(r, 1);
      gridLayout_->setColumnStretch(c, 1);
    }
  }

  // 6. After placing cells, auto-assign any active, currently unassigned cameras
  for (const auto &cam : knownCameras_) {
    if (cam.active) {
      bool isAssigned = false;
      // Check if it's already in one of the surviving/new cells
      for (const auto &cell : cells_) {
        if (cell.assignedCamera == cam.name || 
            (cell.streamPicker && cell.streamPicker->currentText() == cam.name)) {
          isAssigned = true;
          break;
        }
      }

      // If active but homeless, find a home for it
      if (!isAssigned) {
        for (auto &cell : cells_) {
          if (cell.assignedCamera.isEmpty() && 
             (cell.streamPicker && cell.streamPicker->currentIndex() <= 0)) { // 0 is "— None —"
            int idx = cell.streamPicker->findText(cam.name);
            if (idx >= 0) {
              cell.streamPicker->setCurrentIndex(idx);
              break; // Found a home
            }
          }
        }
      }
    }
  }
}

// ─── Layout changed ────────────────────────────────────────────────

void CameraWidget::onLayoutChanged(int index) {
  int rows, cols;
  parseLayout(index, rows, cols);
  rebuildGrid(rows, cols);
}

// ─── Size changed ──────────────────────────────────────────────────

void CameraWidget::onSizeChanged(int /*index*/) {
  // Rebuild with current layout to apply new size
  int rows, cols;
  parseLayout(layoutCombo_->currentIndex(), rows, cols);
  rebuildGrid(rows, cols);
}

// ─── Camera list from ROS ──────────────────────────────────────────

void CameraWidget::onCamerasUpdated(CameraStatusList cameras) {
  QList<CameraInfo> newlyActive;

  for (const auto &cam : cameras) {
    if (cam.active) {
      // Check if it was newly turned active
      bool wasActive = false;
      for (const auto &oldCam : knownCameras_) {
        if (oldCam.name == cam.name && oldCam.active) {
          wasActive = true;
          break;
        }
      }
      if (!wasActive) newlyActive.append(cam);
    } else {
      // If camera became inactive, unassign it from any cells
      for (int i = 0; i < cells_.size(); ++i) {
        if (cells_[i].assignedCamera == cam.name) {
          cells_[i].streamPicker->setCurrentIndex(0); // "None"
        }
      }
    }
  }

  knownCameras_ = cameras;
  updateCellDropdowns();

  // Auto-assign newly active cameras to first available empty cells
  for (const auto &cam : newlyActive) {
    bool alreadyAssigned = false;
    for (int i = 0; i < cells_.size(); ++i) {
      if (cells_[i].assignedCamera == cam.name) {
        alreadyAssigned = true;
        break;
      }
    }

    if (!alreadyAssigned) {
      for (int i = 0; i < cells_.size(); ++i) {
        if (cells_[i].assignedCamera.isEmpty()) {
          int idx = cells_[i].streamPicker->findText(cam.name);
          if (idx >= 0) {
            cells_[i].streamPicker->setCurrentIndex(idx);
            break; 
          }
        }
      }
    }
  }
  
  // Clear existing sidebar buttons
  QLayoutItem *child;
  for (int i = camerasListLayout_->count() - 1; i >= 0; --i) {
    child = camerasListLayout_->itemAt(i);
    if (child->widget()) {
      delete child->widget();
    }
  }

  // Populate sidebar buttons
  for (const auto &camInfo : knownCameras_) {
    QWidget *rowWidget = new QWidget(ui->sidebarScrollContents);
    QHBoxLayout *rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(2);

    QPushButton *btn = new QPushButton(camInfo.name, rowWidget);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    btn->setProperty("cameraName", camInfo.name);
    btn->setProperty("isActive", camInfo.active);
    btn->setProperty("port", camInfo.port);

    QString btnStyle = R"(
        QPushButton { 
            color: #AAAAAA; padding: 8px 12px; border: 1px solid #333333; 
            border-radius: 0px; font-size: 11px; background-color: #1C1C1C; 
            text-align: left;
        }
        QPushButton:hover { background-color: #252525; color: #FFFFFF; }
    )";

    if (camInfo.active) {
      btn->setStyleSheet(R"(
          QPushButton { 
              color: #FFFF55; padding: 8px 12px; border: 1px solid #FFFF55; 
              border-radius: 0px; font-size: 11px; background-color: #252525; 
              text-align: left;
          }
          QPushButton:hover { background-color: #333333; }
      )");
      btn->setText("■ " + camInfo.name + " (ACTIVE)");
    } else {
      btn->setStyleSheet(btnStyle);
      btn->setText("▶ " + camInfo.name + " (OFF)");
    }

    connect(btn, &QPushButton::clicked, this, &CameraWidget::onSidebarCameraClicked);
    rowLayout->addWidget(btn);

    // Dummy settings dropdown
    QToolButton *settingsBtn = new QToolButton(rowWidget);
    settingsBtn->setText("⚙");
    settingsBtn->setStyleSheet(R"(
        QToolButton {
            color: #AAAAAA; background-color: #1C1C1C;
            border: 1px solid #333333; border-radius: 0px;
            padding: 8px 4px; font-size: 14px;
        }
        QToolButton:hover { background-color: #333333; color: #FFFFFF; }
        QToolButton::menu-indicator { image: none; }
    )");
    settingsBtn->setPopupMode(QToolButton::InstantPopup);
    QMenu *settingsMenu = new QMenu(settingsBtn);
    settingsMenu->addAction("Configure... (TODO)");
    settingsBtn->setMenu(settingsMenu);
    
    rowLayout->addWidget(settingsBtn);

    // Insert before spacer
    camerasListLayout_->insertWidget(camerasListLayout_->count() - 1, rowWidget);
  }

}



// ─── Sidebar actions ───────────────────────────────────────────────

void CameraWidget::onSidebarToggle() {
  QList<int> sizes = ui->mainSplitter->sizes();
  if (sizes.value(0, 0) == 0) {
    // Hidden -> Show (restore to 250px)
    ui->mainSplitter->setSizes(QList<int>() << 250 << ui->mainSplitter->width() - 250);
  } else {
    // Shown -> Hide
    ui->mainSplitter->setSizes(QList<int>() << 0 << ui->mainSplitter->width());
  }
}

void CameraWidget::onSidebarCameraClicked() {
  QPushButton *btn = qobject_cast<QPushButton *>(sender());
  if (!btn)
    return;

  QString name = btn->property("cameraName").toString();
  bool isActive = btn->property("isActive").toBool();

  if (isActive) {
    // Unassign from any cells displaying this stream
    for (int i = 0; i < cells_.size(); ++i) {
      if (cells_[i].assignedCamera == name) {
        cells_[i].streamPicker->setCurrentIndex(0); // "None"
      }
    }
    QMetaObject::invokeMethod(rosWorker_, "callStopStream",
                              Qt::QueuedConnection, Q_ARG(QString, name));
  } else {
    QMetaObject::invokeMethod(rosWorker_, "callStartStream",
                              Qt::QueuedConnection, Q_ARG(QString, name));
  }
}

// ─── Hotkeys ───────────────────────────────────────────────────────

void CameraWidget::setupHotkeys() {
  // Ctrl + 1..9 to toggle camera 1..9
  for (int i = 0; i < 9; ++i) {
    QShortcut *shortcut = new QShortcut(QKeySequence(QString("Ctrl+%1").arg(i + 1)), this);
    connect(shortcut, &QShortcut::activated, this, [this, i]() {
      toggleCameraByIndex(i);
    });
  }

  // Ctrl + A to toggle all cameras
  QShortcut *toggleAllShortcut = new QShortcut(QKeySequence("Ctrl+A"), this);
  connect(toggleAllShortcut, &QShortcut::activated, this, &CameraWidget::toggleAllCameras);

  // Ctrl + R to focus layout dropdown
  QShortcut *focusLayoutShortcut = new QShortcut(QKeySequence("Ctrl+R"), this);
  connect(focusLayoutShortcut, &QShortcut::activated, this, [this]() {
    if (layoutCombo_) {
      layoutCombo_->setFocus();
      layoutCombo_->showPopup();
    }
  });
}

void CameraWidget::toggleCameraByIndex(int index) {
  if (index < 0 || index >= knownCameras_.size())
    return;

  // We want to simulate clicking the corresponding sidebar button.
  // The sidebar buttons are in camerasListLayout_ and correspond 1:1 with knownCameras_
  // but let's just trigger the service call directly based on knownCameras_[index] state
  const CameraInfo &cam = knownCameras_[index];
  
  if (cam.active) {
    // Unassign from any cells displaying this stream
    for (int i = 0; i < cells_.size(); ++i) {
      if (cells_[i].assignedCamera == cam.name) {
        cells_[i].streamPicker->setCurrentIndex(0); // "None"
      }
    }
    QMetaObject::invokeMethod(rosWorker_, "callStopStream",
                              Qt::QueuedConnection, Q_ARG(QString, cam.name));
  } else {
    QMetaObject::invokeMethod(rosWorker_, "callStartStream",
                              Qt::QueuedConnection, Q_ARG(QString, cam.name));
  }
}

void CameraWidget::toggleAllCameras() {
  if (knownCameras_.isEmpty()) return;

  // Check if *all* are currently active
  bool allActive = true;
  for (const auto &cam : knownCameras_) {
    if (!cam.active) {
      allActive = false;
      break;
    }
  }

  if (allActive) {
    // Turn all OFF
    for (const auto &cam : knownCameras_) {
      for (int i = 0; i < cells_.size(); ++i) {
        if (cells_[i].assignedCamera == cam.name) {
          cells_[i].streamPicker->setCurrentIndex(0);
        }
      }
      QMetaObject::invokeMethod(rosWorker_, "callStopStream",
                                Qt::QueuedConnection, Q_ARG(QString, cam.name));
    }
  } else {
    // Turn all ON (that are currently off)
    for (const auto &cam : knownCameras_) {
      if (!cam.active) {
        QMetaObject::invokeMethod(rosWorker_, "callStartStream",
                                  Qt::QueuedConnection, Q_ARG(QString, cam.name));
      }
    }
  }
}



// ─── Update all cell dropdowns with current camera list ────────────

void CameraWidget::updateCellDropdowns() {
  for (int i = 0; i < cells_.size(); ++i) {
    QComboBox *picker = cells_[i].streamPicker;
    if (!picker)
      continue;

    // Save current selection
    QString currentSelection = cells_[i].assignedCamera;

    // Block signals while rebuilding items
    picker->blockSignals(true);
    picker->clear();
    picker->addItem("— None —");

    int restoreIndex = 0;
    for (int j = 0; j < knownCameras_.size(); ++j) {
      picker->addItem(knownCameras_[j].name);
      if (knownCameras_[j].name == currentSelection) {
        restoreIndex = j + 1; // +1 because "None" is at index 0
      }
    }

    picker->setCurrentIndex(restoreIndex);
    picker->blockSignals(false);
  }
}

// ─── User changed which stream a cell shows ────────────────────────

void CameraWidget::onCellStreamChanged(int cellIndex) {
  if (cellIndex < 0 || cellIndex >= cells_.size())
    return;

  GridCell &cell = cells_[cellIndex];
  QComboBox *picker = cell.streamPicker;
  if (!picker || !cell.player)
    return;

  int pickerIndex = picker->currentIndex();

  // Stop old stream if any
  if (!cell.assignedCamera.isEmpty()) {
    cell.player->stopStream();
    cell.assignedCamera = "";
  }

  // "None" selected
  if (pickerIndex <= 0) {
    return;
  }

  // Start new stream locally
  int camIndex = pickerIndex - 1; // offset for "None"
  if (camIndex < 0 || camIndex >= knownCameras_.size())
    return;

  const CameraInfo &cam = knownCameras_[camIndex];
  cell.assignedCamera = cam.name;

  cell.player->switchStream(cam.name, cam.port);
}


