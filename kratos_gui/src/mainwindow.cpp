#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDebug>
#include <QMenu>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QStyle>
#include <QTabBar>

#include "camerawidget.h"
#include "joynodewidget.h"

#include <QApplication>
#include <QShortcut>
#include <QKeySequence>
#include <QDialog>
#include <QListWidget>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QTimer>

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
    listWidget->setCurrentRow(0); // Select first by default
    layout->addWidget(listWidget);

    setFixedSize(250, 130);
  }

  int getSelectedIndex() const { return listWidget->currentRow(); }

protected:
  void keyPressEvent(QKeyEvent *event) override {
    if (event->key() == Qt::Key_Escape) {
      reject();
    } else if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return) {
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
    } else {
      QDialog::keyPressEvent(event);
    }
  }

private:
  QListWidget *listWidget;
};
// ───────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent)
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

  // Setup Dummy Add Tab
  int addTabIndex = ui->tabWidget->addTab(new QWidget(), " + ");
  QTabBar *tabBar = ui->tabWidget->findChild<QTabBar *>();
  if (tabBar) {
    tabBar->setTabButton(addTabIndex, QTabBar::RightSide, nullptr);
  }

  connect(ui->tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
    if (index == ui->tabWidget->count() - 1 && index >= 0) {
      int prev = (ui->tabWidget->count() > 1) ? index - 1 : -1;
      if (prev >= 0) {
        ui->tabWidget->setCurrentIndex(prev);
      } else {
        ui->stackedWidget->setCurrentIndex(0);
      }
    }
  });

  connect(ui->tabWidget, &QTabWidget::tabBarClicked, this, [this](int index) {
    if (index == ui->tabWidget->count() - 1) {
      QMenu addMenu(this);
      addMenu.setStyleSheet(this->styleSheet());
      connect(addMenu.addAction("Add Cam View"), &QAction::triggered, this,
              &MainWindow::onAddCamClicked);
      connect(addMenu.addAction("Add Joynode Panel"), &QAction::triggered, this,
              &MainWindow::onAddJoyNodeClicked);
      connect(addMenu.addAction("Add Settings Panel"), &QAction::triggered, this,
              &MainWindow::onAddSettingsClicked);
      addMenu.exec(QCursor::pos());
    }
  });

  connect(ui->tabWidget, &QTabWidget::tabCloseRequested, this,
          &MainWindow::onTabCloseRequested);

  ui->stackedWidget->setCurrentIndex(0); // Show empty state initially

  // Set up ROS Worker
  rosWorker_ = new RosWorker();
  rosWorker_->moveToThread(&rosThread_);
  connect(&rosThread_, &QThread::started, rosWorker_, &RosWorker::init);
  connect(&rosThread_, &QThread::finished, rosWorker_, &QObject::deleteLater);

  rosThread_.start();

  // ── Application Hotkeys ──────────────────────────────────────────
  
  // Close current tab: Ctrl + W
  QShortcut *closeTabShortcut = new QShortcut(QKeySequence("Ctrl+W"), this);
  connect(closeTabShortcut, &QShortcut::activated, this, [this]() {
    int idx = ui->tabWidget->currentIndex();
    if (idx >= 0 && idx < ui->tabWidget->count() - 1) { // Don't close dummy + tab
      onTabCloseRequested(idx);
    }
  });

  // Next tab: Ctrl + Tab
  QShortcut *nextTabShortcut = new QShortcut(QKeySequence("Ctrl+Tab"), this);
  connect(nextTabShortcut, &QShortcut::activated, this, [this]() {
    int count = ui->tabWidget->count();
    if (count > 1) { // 1 means only dummy + tab exists
      int current = ui->tabWidget->currentIndex();
      int next = (current + 1) % (count - 1); // Cycle, ignoring dummy tab
      ui->tabWidget->setCurrentIndex(next);
    }
  });

  // Previous tab: Ctrl + Shift + Tab
  QShortcut *prevTabShortcut = new QShortcut(QKeySequence("Ctrl+Shift+Tab"), this);
  connect(prevTabShortcut, &QShortcut::activated, this, [this]() {
    int count = ui->tabWidget->count();
    if (count > 1) {
      int current = ui->tabWidget->currentIndex();
      int prev = (current - 1 + (count - 1)) % (count - 1);
      ui->tabWidget->setCurrentIndex(prev);
    }
  });

  // Quit Application: Ctrl + Shift + Q
  QShortcut *quitShortcut = new QShortcut(QKeySequence("Ctrl+Shift+Q"), this);
  connect(quitShortcut, &QShortcut::activated, this, []() {
    QApplication::quit();
  });

  // Add Tab Menu: Ctrl + T
  QShortcut *addTabShortcut = new QShortcut(QKeySequence("Ctrl+T"), this);
  connect(addTabShortcut, &QShortcut::activated, this, [this]() {
    AddTabDialog dialog(this);
    // Center it on the main window
    dialog.move(geometry().center() - dialog.rect().center());
    if (dialog.exec() == QDialog::Accepted) {
      if (dialog.getSelectedIndex() == 0) {
        onAddCamClicked();
      } else if (dialog.getSelectedIndex() == 1) {
        onAddJoyNodeClicked();
      } else if (dialog.getSelectedIndex() == 2) {
        onAddSettingsClicked();
      }
    }
  });

  // Help Menu Overlay: /
  QShortcut *helpShortcut = new QShortcut(QKeySequence("/"), this);
  connect(helpShortcut, &QShortcut::activated, this, [this]() {
    QDialog overlay(this);
    overlay.setWindowTitle("Keyboard Shortcuts");
    overlay.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    overlay.setStyleSheet(R"(
      QDialog {
          background-color: rgba(20, 20, 20, 240);
          border: 1px solid #555555;
          border-radius: 8px;
      }
      QLabel {
          color: #CCCCCC;
          font-size: 14px;
      }
      QLabel#title {
          color: #FFFF55;
          font-size: 18px;
          font-weight: bold;
          margin-bottom: 10px;
      }
      QLabel#shortcut {
          color: #FFFFFF;
          font-weight: bold;
          background-color: #333333;
          padding: 2px 6px;
          border-radius: 4px;
      }
    )");

    QVBoxLayout *layout = new QVBoxLayout(&overlay);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(10);

    QLabel *title = new QLabel("Keyboard Shortcuts", &overlay);
    title->setObjectName("title");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto addShortcut = [&layout, &overlay](const QString &key, const QString &desc) {
      QHBoxLayout *row = new QHBoxLayout();
      QLabel *keyLabel = new QLabel(key, &overlay);
      keyLabel->setObjectName("shortcut");
      QLabel *descLabel = new QLabel(desc, &overlay);
      row->addWidget(keyLabel);
      row->addWidget(descLabel);
      row->addStretch();
      layout->addLayout(row);
    };

    addShortcut("Ctrl + Tab", "Next Tab");
    addShortcut("Ctrl + Shift + Tab", "Previous Tab");
    addShortcut("Ctrl + T", "Add New Tab");
    addShortcut("Ctrl + W", "Close Current Tab");
    addShortcut("Ctrl + Shift + Q", "Quit Application");
    addShortcut("Ctrl + 1..9", "Toggle Camera 1..9 (Camera Tab Only)");
    addShortcut("Ctrl + A", "Toggle All Cameras (Camera Tab Only)");
    addShortcut("Ctrl + R", "Focus Layout Dropdown (Camera Tab Only)");
    addShortcut("/", "Show this help menu");

    QLabel *footer = new QLabel("\nPress / or Esc to close", &overlay);
    footer->setAlignment(Qt::AlignCenter);
    footer->setStyleSheet("color: #888888; font-size: 12px;");
    layout->addWidget(footer);

    overlay.adjustSize();
    overlay.move(geometry().center() - overlay.rect().center());

    QShortcut *closeHelp1 = new QShortcut(QKeySequence("/"), &overlay);
    connect(closeHelp1, &QShortcut::activated, &overlay, &QDialog::accept);
    
    QShortcut *closeHelp2 = new QShortcut(QKeySequence("Esc"), &overlay);
    connect(closeHelp2, &QShortcut::activated, &overlay, &QDialog::accept);

    overlay.exec();
  });
}

MainWindow::~MainWindow() {

  rosThread_.quit();
  rosThread_.wait();

  delete ui;
}

void MainWindow::setupAddMenu(QPushButton *btn) {
  QMenu *addMenu = new QMenu(this);
  connect(addMenu->addAction("Add Cam View"), &QAction::triggered, this,
          &MainWindow::onAddCamClicked);
  connect(addMenu->addAction("Add Joynode Panel"), &QAction::triggered, this,
          &MainWindow::onAddJoyNodeClicked);
  btn->setMenu(addMenu);
}

void MainWindow::onAddCamClicked() {
  ui->stackedWidget->setCurrentIndex(1); // Switch to tabs view
  int targetIndex = ui->tabWidget->count() - 1;
  if (targetIndex < 0)
    targetIndex = 0;

  int nextNum = 1;
  while (true) {
    bool numUsed = false;
    QString nameToCheck = QString("CAMS %1").arg(nextNum);
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
      if (ui->tabWidget->tabText(i) == nameToCheck) {
        numUsed = true;
        break;
      }
    }
    if (!numUsed)
      break;
    nextNum++;
  }

  CameraWidget *cw = new CameraWidget(rosWorker_, this);
  int index = ui->tabWidget->insertTab(targetIndex, cw,
                                       QString("CAMS %1").arg(nextNum));

  QTabBar *tabBar = ui->tabWidget->findChild<QTabBar *>();
  if (tabBar) {
    QPushButton *closeBtn = new QPushButton("✕", ui->tabWidget);
    closeBtn->setFixedSize(16, 16);
    closeBtn->setStyleSheet(
        "QPushButton { color: #AAAAAA; border: none; background: transparent; "
        "font-size: 12px; font-weight: bold; } QPushButton:hover { color: "
        "#FFFF55; }");
    tabBar->setTabButton(index, QTabBar::RightSide, closeBtn);
    connect(closeBtn, &QPushButton::clicked, this, [this, cw]() {
      int idx = ui->tabWidget->indexOf(cw);
      if (idx != -1)
        QTimer::singleShot(0, this, [this, idx]() { onTabCloseRequested(idx); });
    });
  }

  ui->tabWidget->setCurrentIndex(index);
}

void MainWindow::onAddJoyNodeClicked() {
  ui->stackedWidget->setCurrentIndex(1); // Switch to tabs view
  int targetIndex = ui->tabWidget->count() - 1;
  if (targetIndex < 0)
    targetIndex = 0;

  int nextNum = 1;
  while (true) {
    bool numUsed = false;
    QString nameToCheck = QString("JOYNODE %1").arg(nextNum);
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
      if (ui->tabWidget->tabText(i) == nameToCheck) {
        numUsed = true;
        break;
      }
    }
    if (!numUsed)
      break;
    nextNum++;
  }

  JoyNodeWidget *jw = new JoyNodeWidget(this);
  int index = ui->tabWidget->insertTab(targetIndex, jw,
                                       QString("JOYNODE %1").arg(nextNum));

  connect(rosWorker_, &RosWorker::joystickDataUpdated, jw,
          &JoyNodeWidget::updateJoystickData);

  QTabBar *tabBar = ui->tabWidget->findChild<QTabBar *>();
  if (tabBar) {
    QPushButton *closeBtn = new QPushButton("✕", ui->tabWidget);
    closeBtn->setFixedSize(16, 16);
    closeBtn->setStyleSheet(
        "QPushButton { color: #AAAAAA; border: none; background: transparent; "
        "font-size: 12px; font-weight: bold; } QPushButton:hover { color: "
        "#FFFF55; }");
    tabBar->setTabButton(index, QTabBar::RightSide, closeBtn);
    connect(closeBtn, &QPushButton::clicked, this, [this, jw]() {
      int idx = ui->tabWidget->indexOf(jw);
      if (idx != -1)
        QTimer::singleShot(0, this, [this, idx]() { onTabCloseRequested(idx); });
    });
  }

  ui->tabWidget->setCurrentIndex(index);
}

void MainWindow::onAddSettingsClicked() {
  ui->stackedWidget->setCurrentIndex(1); // Switch to tabs view
  int targetIndex = ui->tabWidget->count() - 1;
  if (targetIndex < 0)
    targetIndex = 0;

  int nextNum = 1;
  while (true) {
    bool numUsed = false;
    QString nameToCheck = QString("SETTINGS %1").arg(nextNum);
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
      if (ui->tabWidget->tabText(i) == nameToCheck) {
        numUsed = true;
        break;
      }
    }
    if (!numUsed)
      break;
    nextNum++;
  }

  QWidget *settingsTab = new QWidget(this);
  settingsTab->setStyleSheet("background-color: #0A0A0A;");

  QVBoxLayout *layout = new QVBoxLayout(settingsTab);
  
  QLabel *todoLabel = new QLabel("⚙️ TODO: Settings Panel", settingsTab);
  todoLabel->setStyleSheet("color: #666666; font-size: 24px; font-weight: bold;");
  todoLabel->setAlignment(Qt::AlignCenter);

  layout->addWidget(todoLabel);
  
  int index = ui->tabWidget->insertTab(targetIndex, settingsTab,
                                       QString("SETTINGS %1").arg(nextNum));

  QTabBar *tabBar = ui->tabWidget->findChild<QTabBar *>();
  if (tabBar) {
    QPushButton *closeBtn = new QPushButton("✕", ui->tabWidget);
    closeBtn->setFixedSize(16, 16);
    closeBtn->setStyleSheet(
        "QPushButton { color: #AAAAAA; border: none; background: transparent; "
        "font-size: 12px; font-weight: bold; } QPushButton:hover { color: "
        "#FFFF55; }");
    tabBar->setTabButton(index, QTabBar::RightSide, closeBtn);
    connect(closeBtn, &QPushButton::clicked, this, [this, settingsTab]() {
      int idx = ui->tabWidget->indexOf(settingsTab);
      if (idx != -1)
        QTimer::singleShot(0, this, [this, idx]() { onTabCloseRequested(idx); });
    });
  }

  ui->tabWidget->setCurrentIndex(index);
}

void MainWindow::onTabCloseRequested(int index) {
  if (index == ui->tabWidget->count() - 1)
    return; // Prevent closing the dummy tab
  QWidget *widget = ui->tabWidget->widget(index);
  ui->tabWidget->removeTab(index);
  widget->deleteLater();

  if (ui->tabWidget->count() <= 1) {       // Only the dummy left
    ui->stackedWidget->setCurrentIndex(0); // Revert to empty state
  }
}
