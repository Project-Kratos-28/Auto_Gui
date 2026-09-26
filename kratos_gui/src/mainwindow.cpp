#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QApplication>
#include <QDebug>
#include <QDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QShortcut>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>

#include "camerawidget.h"
#include "joynodewidget.h"
#include "mapwidget.h"
#include "missionpanel.h"
#include "tuipanel.h"
#include "waypointpanel.h"

static bool _registered = []() {
  qRegisterMetaType<CameraStatusList>("CameraStatusList");
  qRegisterMetaType<QImage>("QImage");
  qRegisterMetaType<QList<float>>("QList<float>");
  qRegisterMetaType<QList<int>>("QList<int>");
  return true;
}();

// ─── Custom Dialog for Add Tab (Ctrl+T) ────────────────────────────
class AddTabDialog : public QDialog {
public:
  explicit AddTabDialog(QWidget *parent = nullptr) : QDialog(parent) {
    setWindowTitle("Add New Tab");
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

    setStyleSheet(R"(
      QDialog {
          background-color: #1A1A1A;
          border: 1px solid #555555;
      }

      QListWidget {
          background-color: transparent;
          color: #CCCCCC;
          border: none;
          font-size: 14px;
      }

      QListWidget::item {
          padding: 8px 12px;
      }

      QListWidget::item:selected {
          background-color: #333333;
          color: #FFFF55;
          border-left: 3px solid #FFFF55;
      }
    )");

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);

    listWidget = new QListWidget(this);

    listWidget->addItem("Add Cam View (C)");
    listWidget->addItem("Add Joynode Panel (J)");
    listWidget->addItem("Add Settings Panel (S)");
    listWidget->addItem("Add TUI Panel (T)");
    listWidget->addItem("Open Map View (M)");

    listWidget->setCurrentRow(0);

    layout->addWidget(listWidget);

    setFixedSize(250, 200);
  }

  int getSelectedIndex() const {
    return listWidget->currentRow();
  }

protected:
  void keyPressEvent(QKeyEvent *event) override {
    if (event->key() == Qt::Key_Escape) {
      reject();
    } else if (event->key() == Qt::Key_Enter ||
               event->key() == Qt::Key_Return) {
      accept();
    } else if (event->key() == Qt::Key_C) {
      listWidget->setCurrentRow(0);
      accept();
    } else if (event->key() == Qt::Key_J) {
      listWidget->setCurrentRow(1);
      accept();
    } else if (event->key() == Qt::Key_S) {
      listWidget->setCurrentRow(2);
      accept();
    } else if (event->key() == Qt::Key_T) {
      listWidget->setCurrentRow(3);
      accept();
    } else if (event->key() == Qt::Key_M) {
      listWidget->setCurrentRow(4);
      accept();
    } else {
      QDialog::keyPressEvent(event);
    }
  }

private:
  QListWidget *listWidget;
};
// ───────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QApplication *app, QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {

  ui->setupUi(this);

  // Styling for Nvidia-Isaac Sim dark aesthetic
  setStyleSheet(R"(
    QMainWindow {
        background-color: #121212;
        font-family: 'Roboto', 'Segoe UI', Arial, sans-serif;
    }

    QPushButton#bigAddBtn {
        color: #FFFF55;
        background-color: #1C1C1C;
        border: 1px solid #333333;
        border-radius: 0px;
        font-size: 14px;
        padding: 10px 20px;
    }

    QPushButton#bigAddBtn:hover {
        border: 1px solid #FFFF55;
        background-color: #252525;
    }

    QPushButton#bigAddBtn::menu-indicator {
        image: none;
    }

    QTabWidget::pane {
        border-top: 1px solid #333333;
        background-color: #121212;
    }

    QTabBar {
        background-color: #000000;
    }

    QTabBar::tab {
        background-color: #1C1C1C;
        color: #AAAAAA;
        border: 1px solid #333333;
        border-radius: 0px;
        padding: 5px 12px;
        margin-right: 1px;
        font-size: 11px;
    }

    QTabBar::tab:selected {
        color: #FFFFFF;
        background-color: #252525;
        border-top: 1px solid #FFFF55;
        border-bottom: 1px solid #252525;
    }

    QTabBar::tab:hover:!selected {
        color: #CCCCCC;
        background-color: #2A2A2A;
    }

    QStatusBar {
        background: #1C1C1C;
        color: #AAAAAA;
        border-top: 1px solid #333333;
        font-size: 11px;
    }

    QMenu {
        background-color: #1C1C1C;
        color: #CCCCCC;
        border: 1px solid #333333;
        border-radius: 0px;
        font-size: 11px;
    }

    QMenu::item {
        padding: 6px 20px;
    }

    QMenu::item:selected {
        background-color: #252525;
        color: #FFFF55;
        border-left: 2px solid #FFFF55;
    }
  )");

  setupAddMenu(ui->bigAddBtn);

  // ───────── Dummy "+" tab ─────────

  int addTabIndex =
      ui->tabWidget->addTab(new QWidget(), " + ");

  QTabBar *tabBar =
      ui->tabWidget->findChild<QTabBar *>();

  if (tabBar) {
    tabBar->setTabButton(
        addTabIndex,
        QTabBar::RightSide,
        nullptr);
  }

  // ───────── Handle "+" tab selection ─────────

  connect(ui->tabWidget,
          &QTabWidget::currentChanged,
          this,
          [this](int index) {

            if (index == ui->tabWidget->count() - 1 &&
                index >= 0) {

              int prev =
                  (ui->tabWidget->count() > 1)
                      ? index - 1
                      : -1;

              if (prev >= 0) {
                ui->tabWidget->setCurrentIndex(prev);
              } else {
                ui->stackedWidget->setCurrentIndex(0);
              }
            }
          });

  // ───────── "+" tab menu ─────────

  connect(ui->tabWidget,
          &QTabWidget::tabBarClicked,
          this,
          [this](int index) {

            if (index ==
                ui->tabWidget->count() - 1) {

              QMenu addMenu(this);

              addMenu.setStyleSheet(
                  this->styleSheet());

              connect(
                  addMenu.addAction("Add Cam View"),
                  &QAction::triggered,
                  this,
                  &MainWindow::onAddCamClicked);

              connect(
                  addMenu.addAction("Add Joynode Panel"),
                  &QAction::triggered,
                  this,
                  &MainWindow::onAddJoyNodeClicked);

              connect(
                  addMenu.addAction("Add Settings Panel"),
                  &QAction::triggered,
                  this,
                  &MainWindow::onAddSettingsClicked);

              connect(
                  addMenu.addAction("Add TUI Panel"),
                  &QAction::triggered,
                  this,
                  &MainWindow::onAddTuiClicked);

              connect(
                  addMenu.addAction("Open Map View"),
                  &QAction::triggered,
                  this,
                  &MainWindow::onOpenMapClicked);

              addMenu.exec(QCursor::pos());
            }
          });

  connect(ui->tabWidget,
          &QTabWidget::tabCloseRequested,
          this,
          &MainWindow::onTabCloseRequested);

  ui->stackedWidget->setCurrentIndex(0);

  // ───────── ROS worker ─────────

  rosWorker_ = new RosWorker();

  rosWorker_->moveToThread(&rosThread_);

  connect(
      &rosThread_,
      &QThread::started,
      rosWorker_,
      &RosWorker::init);

  connect(
      &rosThread_,
      &QThread::finished,
      rosWorker_,
      &QObject::deleteLater);

  rosThread_.start();

  // ───────── Map ─────────

  mapWidget_ =
      new MapWidget(app, rosWorker_, this);

  onOpenMapClicked();
}

// ───────────────────────────────────────────────────────────────────
// CLOSE
// ───────────────────────────────────────────────────────────────────

void MainWindow::closeEvent(QCloseEvent *event) {

  if (rosThread_.isRunning()) {
    rosThread_.quit();
    rosThread_.wait();
  }

  event->accept();
}

// ───────────────────────────────────────────────────────────────────
// ADD MENU
// ───────────────────────────────────────────────────────────────────

void MainWindow::setupAddMenu(QPushButton *btn) {

  QMenu *addMenu = new QMenu(this);

  connect(
      addMenu->addAction("Add Cam View"),
      &QAction::triggered,
      this,
      &MainWindow::onAddCamClicked);

  connect(
      addMenu->addAction("Add Joynode Panel"),
      &QAction::triggered,
      this,
      &MainWindow::onAddJoyNodeClicked);

  connect(
      addMenu->addAction("Add TUI Panel"),
      &QAction::triggered,
      this,
      &MainWindow::onAddTuiClicked);

  connect(
      addMenu->addAction("Open Map View"),
      &QAction::triggered,
      this,
      &MainWindow::onOpenMapClicked);

  btn->setMenu(addMenu);
}

// ───────────────────────────────────────────────────────────────────
// CAMERA PANEL
// ───────────────────────────────────────────────────────────────────

void MainWindow::onAddCamClicked() {

  ui->stackedWidget->setCurrentIndex(1);

  int targetIndex =
      ui->tabWidget->count() - 1;

  if (targetIndex < 0)
    targetIndex = 0;

  int nextNum = 1;

  while (true) {

    bool numUsed = false;

    QString nameToCheck =
        QString("CAMS %1").arg(nextNum);

    for (int i = 0;
         i < ui->tabWidget->count();
         ++i) {

      if (ui->tabWidget->tabText(i) ==
          nameToCheck) {

        numUsed = true;
        break;
      }
    }

    if (!numUsed)
      break;

    nextNum++;
  }

  CameraWidget *cw =
      new CameraWidget(rosWorker_, this);

  int index =
      ui->tabWidget->insertTab(
          targetIndex,
          cw,
          QString("CAMS %1").arg(nextNum));

  QTabBar *tabBar =
      ui->tabWidget->findChild<QTabBar *>();

  if (tabBar) {

    QPushButton *closeBtn =
        new QPushButton("✕", ui->tabWidget);

    closeBtn->setFixedSize(16, 16);

    closeBtn->setStyleSheet(
        "QPushButton { "
        "color: #AAAAAA; "
        "border: none; "
        "background: transparent; "
        "font-size: 12px; "
        "font-weight: bold; "
        "} "
        "QPushButton:hover { "
        "color: #FFFF55; "
        "}");

    tabBar->setTabButton(
        index,
        QTabBar::RightSide,
        closeBtn);

    connect(
        closeBtn,
        &QPushButton::clicked,
        this,
        [this, cw]() {

          int idx =
              ui->tabWidget->indexOf(cw);

          if (idx != -1) {

            QTimer::singleShot(
                0,
                this,
                [this, idx]() {
                  onTabCloseRequested(idx);
                });
          }
        });
  }

  ui->tabWidget->setCurrentIndex(index);
}

// ───────────────────────────────────────────────────────────────────
// JOYNODE PANEL
// ───────────────────────────────────────────────────────────────────

void MainWindow::onAddJoyNodeClicked() {

  ui->stackedWidget->setCurrentIndex(1);

  int targetIndex =
      ui->tabWidget->count() - 1;

  if (targetIndex < 0)
    targetIndex = 0;

  int nextNum = 1;

  while (true) {

    bool numUsed = false;

    QString nameToCheck =
        QString("JOYNODE %1").arg(nextNum);

    for (int i = 0;
         i < ui->tabWidget->count();
         ++i) {

      if (ui->tabWidget->tabText(i) ==
          nameToCheck) {

        numUsed = true;
        break;
      }
    }

    if (!numUsed)
      break;

    nextNum++;
  }

  JoyNodeWidget *jw =
      new JoyNodeWidget(this);

  int index =
      ui->tabWidget->insertTab(
          targetIndex,
          jw,
          QString("JOYNODE %1").arg(nextNum));

  connect(
      rosWorker_,
      &RosWorker::joystickDataUpdated,
      jw,
      &JoyNodeWidget::updateJoystickData);

  QTabBar *tabBar =
      ui->tabWidget->findChild<QTabBar *>();

  if (tabBar) {

    QPushButton *closeBtn =
        new QPushButton("✕", ui->tabWidget);

    closeBtn->setFixedSize(16, 16);

    closeBtn->setStyleSheet(
        "QPushButton { "
        "color: #AAAAAA; "
        "border: none; "
        "background: transparent; "
        "font-size: 12px; "
        "font-weight: bold; "
        "} "
        "QPushButton:hover { "
        "color: #FFFF55; "
        "}");

    tabBar->setTabButton(
        index,
        QTabBar::RightSide,
        closeBtn);

    connect(
        closeBtn,
        &QPushButton::clicked,
        this,
        [this, jw]() {

          int idx =
              ui->tabWidget->indexOf(jw);

          if (idx != -1) {

            QTimer::singleShot(
                0,
                this,
                [this, idx]() {
                  onTabCloseRequested(idx);
                });
          }
        });
  }

  ui->tabWidget->setCurrentIndex(index);
}

// ───────────────────────────────────────────────────────────────────
// SETTINGS PANEL
// ───────────────────────────────────────────────────────────────────

void MainWindow::onAddSettingsClicked() {

  ui->stackedWidget->setCurrentIndex(1);

  int targetIndex =
      ui->tabWidget->count() - 1;

  if (targetIndex < 0)
    targetIndex = 0;

  int nextNum = 1;

  while (true) {

    bool numUsed = false;

    QString nameToCheck =
        QString("SETTINGS %1").arg(nextNum);

    for (int i = 0;
         i < ui->tabWidget->count();
         ++i) {

      if (ui->tabWidget->tabText(i) ==
          nameToCheck) {

        numUsed = true;
        break;
      }
    }

    if (!numUsed)
      break;

    nextNum++;
  }

  QWidget *settingsTab =
      new QWidget(this);

  settingsTab->setStyleSheet(
      "background-color: #0A0A0A;");

  QVBoxLayout *layout =
      new QVBoxLayout(settingsTab);

  QLabel *todoLabel =
      new QLabel(
          "⚙️ TODO: Settings Panel",
          settingsTab);

  todoLabel->setStyleSheet(
      "color: #666666; "
      "font-size: 24px; "
      "font-weight: bold;");

  todoLabel->setAlignment(
      Qt::AlignCenter);

  layout->addWidget(todoLabel);

  int index =
      ui->tabWidget->insertTab(
          targetIndex,
          settingsTab,
          QString("SETTINGS %1").arg(nextNum));

  QTabBar *tabBar =
      ui->tabWidget->findChild<QTabBar *>();

  if (tabBar) {

    QPushButton *closeBtn =
        new QPushButton("✕", ui->tabWidget);

    closeBtn->setFixedSize(16, 16);

    closeBtn->setStyleSheet(
        "QPushButton { "
        "color: #AAAAAA; "
        "border: none; "
        "background: transparent; "
        "font-size: 12px; "
        "font-weight: bold; "
        "} "
        "QPushButton:hover { "
        "color: #FFFF55; "
        "}");

    tabBar->setTabButton(
        index,
        QTabBar::RightSide,
        closeBtn);

    connect(
        closeBtn,
        &QPushButton::clicked,
        this,
        [this, settingsTab]() {

          int idx =
              ui->tabWidget->indexOf(settingsTab);

          if (idx != -1) {

            QTimer::singleShot(
                0,
                this,
                [this, idx]() {
                  onTabCloseRequested(idx);
                });
          }
        });
  }

  ui->tabWidget->setCurrentIndex(index);
}

// ───────────────────────────────────────────────────────────────────
// TUI PANEL
// ───────────────────────────────────────────────────────────────────

void MainWindow::onAddTuiClicked() {

  ui->stackedWidget->setCurrentIndex(1);

  int targetIndex =
      ui->tabWidget->count() - 1;

  if (targetIndex < 0)
    targetIndex = 0;

  int nextNum = 1;

  while (true) {

    bool numUsed = false;

    QString nameToCheck =
        QString("TUI %1").arg(nextNum);

    for (int i = 0;
         i < ui->tabWidget->count();
         ++i) {

      if (ui->tabWidget->tabText(i) ==
          nameToCheck) {

        numUsed = true;
        break;
      }
    }

    if (!numUsed)
      break;

    nextNum++;
  }

  TuiPanel *tui =
      new TuiPanel(this);

  int index =
      ui->tabWidget->insertTab(
          targetIndex,
          tui,
          QString("TUI %1").arg(nextNum));

  QTabBar *tabBar =
      ui->tabWidget->findChild<QTabBar *>();

  if (tabBar) {

    QPushButton *closeBtn =
        new QPushButton("✕", ui->tabWidget);

    closeBtn->setFixedSize(16, 16);

    closeBtn->setStyleSheet(
        "QPushButton { "
        "color: #AAAAAA; "
        "border: none; "
        "background: transparent; "
        "font-size: 12px; "
        "font-weight: bold; "
        "} "
        "QPushButton:hover { "
        "color: #FFFF55; "
        "}");

    tabBar->setTabButton(
        index,
        QTabBar::RightSide,
        closeBtn);

    connect(
        closeBtn,
        &QPushButton::clicked,
        this,
        [this, tui]() {

          int idx =
              ui->tabWidget->indexOf(tui);

          if (idx != -1) {

            QTimer::singleShot(
                0,
                this,
                [this, idx]() {
                  onTabCloseRequested(idx);
                });
          }
        });
  }

  ui->tabWidget->setCurrentIndex(index);
}

// ───────────────────────────────────────────────────────────────────
// MAP
// ───────────────────────────────────────────────────────────────────

void MainWindow::onOpenMapClicked() {

  ui->stackedWidget->setCurrentIndex(1);

  int index =
      ui->tabWidget->indexOf(mapWidget_);

  if (index == -1) {

    index =
        ui->tabWidget->insertTab(
            0,
            mapWidget_,
            "MAP");

    QTabBar *tabBar =
        ui->tabWidget->findChild<QTabBar *>();

    if (tabBar) {

      QPushButton *closeBtn =
          new QPushButton("✕", ui->tabWidget);

      closeBtn->setFixedSize(16, 16);

      closeBtn->setStyleSheet(
          "QPushButton { "
          "color: #AAAAAA; "
          "border: none; "
          "background: transparent; "
          "font-size: 12px; "
          "font-weight: bold; "
          "} "
          "QPushButton:hover { "
          "color: #FFFF55; "
          "}");

      tabBar->setTabButton(
          index,
          QTabBar::RightSide,
          closeBtn);

      connect(
          closeBtn,
          &QPushButton::clicked,
          this,
          [this]() {

            int idx =
                ui->tabWidget->indexOf(mapWidget_);

            if (idx != -1) {

              QTimer::singleShot(
                  0,
                  this,
                  [this, idx]() {
                    onTabCloseRequested(idx);
                  });
            }
          });
    }
  }

  ui->tabWidget->setCurrentIndex(index);
}

// ───────────────────────────────────────────────────────────────────
// TAB CLOSE
// ───────────────────────────────────────────────────────────────────

void MainWindow::onTabCloseRequested(int index) {

  if (index < 0 ||
      index >= ui->tabWidget->count())
    return;

  // Never close the "+" tab
  if (index ==
      ui->tabWidget->count() - 1)
    return;

  QWidget *widget =
      ui->tabWidget->widget(index);

  ui->tabWidget->removeTab(index);

  if (widget != mapWidget_)
    widget->deleteLater();

  // If there are no real tabs left,
  // return to the empty/start screen.
  if (ui->tabWidget->count() == 1) {
    ui->stackedWidget->setCurrentIndex(0);
  }
}
