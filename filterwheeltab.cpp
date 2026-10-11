#include "filterwheeltab.h"
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QSettings>
#include <QVBoxLayout>
#include <cstring>

// Default filter names used when QSettings has no saved value for a slot.
static const char *const DEFAULT_FILTER_NAMES[11] = {
    "Clear",
    "Filter 1",  "Filter 2",  "Filter 3",  "Filter 4",  "Filter 5",
    "Filter 6",  "Filter 7",  "Filter 8",  "Filter 9",  "Filter 10"};

FilterWheelTab::FilterWheelTab(andor2k::ClientSocket *&sock, QWidget *parent)
    : QWidget(parent), csock(&sock) {
  createGui();

  m_timer = new QTimer(this);
  connect(m_timer, &QTimer::timeout, this, &FilterWheelTab::update_status_loop);
  // Timer is started in showEvent() — only poll when this tab is visible.
}

void FilterWheelTab::createGui() {
  // ── Left panel: slot selection ──────────────────────────────────────────
  QGroupBox   *slot_box    = new QGroupBox(tr("Filter Slot"));
  QGridLayout *slot_layout = new QGridLayout;
  m_slot_group = new QButtonGroup(this);

  QSettings settings("Helmos", "anguc");
  for (int i = 0; i <= 10; ++i) {
    QString default_name = QString::fromUtf8(DEFAULT_FILTER_NAMES[i]);
    QString saved_name   = settings.value(
        QString("filter_names/%1").arg(i), default_name).toString();

    m_slot_buttons[i] = new QRadioButton(QString::number(i));
    m_filter_names[i] = new QLineEdit(saved_name);
    m_filter_names[i]->setMaxLength(32);
    m_filter_names[i]->setPlaceholderText(default_name);
    m_filter_names[i]->setToolTip(tr("Click to rename this filter slot"));

    // Persist the name whenever the user finishes editing (Enter or focus-out).
    connect(m_filter_names[i], &QLineEdit::editingFinished,
            this, [this, i]() {
              QSettings s("Helmos", "anguc");
              s.setValue(QString("filter_names/%1").arg(i),
                         m_filter_names[i]->text());
            });

    slot_layout->addWidget(m_slot_buttons[i], i, 0);
    slot_layout->addWidget(m_filter_names[i],  i, 1);
    m_slot_group->addButton(m_slot_buttons[i], i);
  }
  m_slot_buttons[0]->setChecked(true);
  // Slot 0 is always "Clear" (both apertures open — no glass in beam).
  // Its meaning is physical, not a swappable filter, so lock the name.
  m_filter_names[0]->setReadOnly(true);
  m_filter_names[0]->setToolTip(tr("Slot 0 is always Clear (no filter)"));
  m_filter_names[0]->setStyleSheet("background: palette(window); color: palette(mid);");
  slot_box->setLayout(slot_layout);

  // ── Right panel: status + controls ──────────────────────────────────────
  QGroupBox *status_box = new QGroupBox(tr("Status"));
  QGridLayout *status_layout = new QGridLayout;

  m_conn_label    = new QLabel(tr("—"));
  m_pos_label     = new QLabel(tr("—"));
  m_homed_label   = new QLabel(tr("No"));
  m_moving_label  = new QLabel(tr("Idle"));
  m_upper_qc_label = new QLabel(tr("0"));
  m_lower_qc_label = new QLabel(tr("0"));
  m_fault_label    = new QLabel(tr("—"));

  status_layout->addWidget(new QLabel(tr("Wheel HW:")),  0, 0);
  status_layout->addWidget(m_conn_label,                  0, 1);
  status_layout->addWidget(new QLabel(tr("Position:")),  1, 0);
  status_layout->addWidget(m_pos_label,                  1, 1);
  status_layout->addWidget(new QLabel(tr("Homed:")),     2, 0);
  status_layout->addWidget(m_homed_label,                2, 1);
  status_layout->addWidget(new QLabel(tr("Motion:")),    3, 0);
  status_layout->addWidget(m_moving_label,               3, 1);
  status_layout->addWidget(new QLabel(tr("Upper QC:")), 4, 0);
  status_layout->addWidget(m_upper_qc_label,             4, 1);
  status_layout->addWidget(new QLabel(tr("Lower QC:")), 5, 0);
  status_layout->addWidget(m_lower_qc_label,             5, 1);
  status_layout->addWidget(new QLabel(tr("Faults:")),   6, 0);
  status_layout->addWidget(m_fault_label,                6, 1);
  status_box->setLayout(status_layout);

  // ── Buttons ──────────────────────────────────────────────────────────────
  m_home_button = new QPushButton(tr("Home"));
  m_stop_button = new QPushButton(tr("Halt"));
  m_go_button   = new QPushButton(tr("Move to Slot"));

  connect(m_home_button, &QPushButton::clicked, this, &FilterWheelTab::go_home);
  connect(m_stop_button, &QPushButton::clicked, this, &FilterWheelTab::stop_motion);
  connect(m_go_button,   &QPushButton::clicked, this, &FilterWheelTab::move_to_slot);

  QHBoxLayout *btn_layout = new QHBoxLayout;
  btn_layout->addWidget(m_home_button);
  btn_layout->addWidget(m_stop_button);
  btn_layout->addWidget(m_go_button);

  // Indeterminate progress bar — always present to avoid layout shifts.
  // Idle: empty static bar (max=1, value=0).
  // Moving: animated indeterminate (min=max=0).
  m_progress = new QProgressBar;
  m_progress->setMinimum(0);
  m_progress->setMaximum(1);
  m_progress->setValue(0);
  m_progress->setTextVisible(true); // show "N%" text inside the bar

  QVBoxLayout *right_layout = new QVBoxLayout;
  right_layout->addWidget(status_box);
  right_layout->addLayout(btn_layout);
  right_layout->addWidget(m_progress);
  right_layout->addStretch();

  // ── Main layout ──────────────────────────────────────────────────────────
  QHBoxLayout *main_layout = new QHBoxLayout;
  main_layout->addWidget(slot_box);
  main_layout->addLayout(right_layout);
  setLayout(main_layout);

  setControlsEnabled(false);
}

void FilterWheelTab::setControlsEnabled(bool enabled) {
  m_home_button->setEnabled(enabled);
  // m_stop_button intentionally NOT here — Halt must remain clickable at all times.
  m_go_button->setEnabled(enabled);
  for (int i = 0; i <= 10; ++i)
    m_slot_buttons[i]->setEnabled(enabled);
}

// ── Slots ────────────────────────────────────────────────────────────────────

void FilterWheelTab::go_home()     { startWheelCommand("wheel home\n"); }
void FilterWheelTab::stop_motion() {
  if (!csock || !*csock)
    return;
  if (m_busy) {
    // WheelThread holds the socket and is reading; we only need to write.
    // The daemon's mid-move MSG_DONTWAIT peek picks this up and calls Halt().
    (*csock)->send("wheel halt\n");
    return;
  }
  startWheelCommand("wheel halt\n");
}

void FilterWheelTab::move_to_slot() {
  if (!csock || !*csock)
    return;
  int slot = m_slot_group->checkedId();
  if (slot < 0 || slot > 10)
    return;
  char cmd[32];
  std::snprintf(cmd, sizeof(cmd), "wheel setpos %d\n", slot);
  startWheelCommand(cmd);
}

void FilterWheelTab::startWheelCommand(const char *cmd) {
  if (!csock || !*csock || m_busy || m_socket_busy)
    return;
  m_busy = true;
  m_timer->stop();
  m_moving_label->setText(tr("Moving"));
  m_moving_label->setStyleSheet(""); // clear any previous error highlight
  // Reset progress bar to indeterminate until daemon sends fmove_qc.
  m_move_qc = 0;
  m_start_upper_qc = m_upper_qc_label->text().toLong();
  m_start_lower_qc = m_lower_qc_label->text().toLong();
  m_progress->setMaximum(0); // indeterminate
  setControlsEnabled(false);
  emit wheelBusy(true); // disables ExposureTab Start button

  WheelThread *t = new WheelThread(*csock, cmd, this);
  connect(t, &WheelThread::progressUpdate, this, &FilterWheelTab::onWheelProgress);
  connect(t, &WheelThread::commandDone, this, &FilterWheelTab::onCommandDone);
  connect(t, &WheelThread::finished, t, &QObject::deleteLater);
  t->start();
}

void FilterWheelTab::onCommandDone(const QString &result) {
  m_busy = false;
  emit wheelBusy(false); // re-enables ExposureTab Start button

  // Parse error code from "done;error:N;[status:MSG;]"
  const QByteArray ba = result.toLocal8Bit();
  const char *ep = std::strstr(ba.constData(), "error:");
  int err_code = ep ? static_cast<int>(std::strtol(ep + 6, nullptr, 10)) : 0;

  if (err_code != 0) {
    // Describe the error in the Motion label (stays visible until next command).
    QString msg;
    switch (err_code) {
      case 2:  msg = tr("Error: not homed");   break;
      case 3:  msg = tr("Error: already moving"); break;
      case 4:  msg = tr("Error: no driver");    break;
      case 10: msg = tr("Error: timeout (60 s)"); break;
      default: msg = tr("Error: %1").arg(err_code); break;
    }
    m_moving_label->setText(msg);
    m_moving_label->setStyleSheet("color: red; font-weight: bold;");
  } else {
    m_moving_label->setText(tr("Idle"));
    m_moving_label->setStyleSheet("");
  }

  // Snap bar to 100% so the user sees completion, then reset to idle.
  if (m_move_qc > 0)
    m_progress->setValue(m_progress->maximum());
  QTimer::singleShot(400, this, [this]() {
    if (!m_busy) { // guard: ignore if a new command started within 400 ms
      m_progress->setMaximum(1);
      m_progress->setValue(0);
    }
  });
  // Restart polling only if the tab is visible and no exposure is running.
  if (!m_socket_busy && isVisible() && csock && *csock) {
    update_status_loop(); // immediate accurate final position after move
    m_timer->start(5000); // 5-second watchdog: detects disconnect while tab visible
  }
}

void FilterWheelTab::onWheelProgress(const QString &update) {
  // Parse intermediate lines from the daemon.
  // "fmove_qc:N;" — sent once at move start; switches bar to determinate.
  // "fupper_qc:N;flower_qc:N;" — sent every 200 ms; updates QC labels + bar.
  const QByteArray ba = update.toLocal8Bit();
  const char *p = ba.constData();
  long cur_upper = m_upper_qc_label->text().toLong();
  long cur_lower = m_lower_qc_label->text().toLong();
  while (p && *p) {
    if (std::strncmp(p, "fmove_qc:", 9) == 0) {
      m_move_qc = std::strtol(p + 9, nullptr, 10);
      if (m_move_qc > 0) {
        m_progress->setMinimum(0);
        m_progress->setMaximum(static_cast<int>(m_move_qc));
        m_progress->setValue(0);
      }
    } else if (std::strncmp(p, "fupper_qc:", 10) == 0) {
      cur_upper = std::strtol(p + 10, nullptr, 10);
      m_upper_qc_label->setText(QString::number(cur_upper));
    } else if (std::strncmp(p, "flower_qc:", 10) == 0) {
      cur_lower = std::strtol(p + 10, nullptr, 10);
      m_lower_qc_label->setText(QString::number(cur_lower));
    }
    p = std::strchr(p, ';');
    if (p) ++p;
  }
  // Update progress bar value when determinate.
  if (m_move_qc > 0) {
    long traveled = std::max(cur_upper - m_start_upper_qc,
                             cur_lower - m_start_lower_qc);
    if (traveled < 0) traveled = 0;
    if (traveled > m_move_qc) traveled = m_move_qc;
    m_progress->setValue(static_cast<int>(traveled));
  }
}

void FilterWheelTab::setSocketBusy(bool busy) {
  m_socket_busy = busy;
  if (busy) {
    m_timer->stop();
    setControlsEnabled(false); // prevent wheel commands during exposure
  } else if (!m_busy && isVisible()) {
    if (csock && *csock)
      update_status_loop(); // immediate state refresh after exposure ends
    m_timer->start(5000);   // 5-second watchdog
  }
}

void FilterWheelTab::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  if (m_socket_busy || m_busy)
    return;
  update_status_loop(); // instant UI refresh when switching to this tab
  m_timer->start(5000);  // 5-second watchdog: detects disconnect while tab visible
}

void FilterWheelTab::hideEvent(QHideEvent *event) {
  QWidget::hideEvent(event);
  if (!m_busy) // keep timer alive if wheel command is still in flight
    m_timer->stop();
}

void FilterWheelTab::parseStatusResponse(const char *response) {
  // Expected: "fconnected:X;fmoving:X;fpos:N;fupper_qc:L;flower_qc:L;fhomed:X;
  //            fupper_fault:X;flower_fault:X"
  int fconnected = 1, fmoving = 0, fpos = -1, fhomed = 0;
  int fupper_fault = 0, flower_fault = 0;
  long fupper_qc = 0, flower_qc = 0;

  const char *p = response;
  while (p && *p) {
    if (std::strncmp(p, "fconnected:", 11) == 0) {
      fconnected = std::strtol(p + 11, nullptr, 10);
    } else if (std::strncmp(p, "fmoving:", 8) == 0) {
      fmoving = std::strtol(p + 8, nullptr, 10);
    } else if (std::strncmp(p, "fpos:", 5) == 0) {
      fpos = static_cast<int>(std::strtol(p + 5, nullptr, 10));
    } else if (std::strncmp(p, "fupper_qc:", 10) == 0) {
      fupper_qc = std::strtol(p + 10, nullptr, 10);
    } else if (std::strncmp(p, "flower_qc:", 10) == 0) {
      flower_qc = std::strtol(p + 10, nullptr, 10);
    } else if (std::strncmp(p, "fhomed:", 7) == 0) {
      fhomed = std::strtol(p + 7, nullptr, 10);
    } else if (std::strncmp(p, "fupper_fault:", 13) == 0) {
      fupper_fault = std::strtol(p + 13, nullptr, 10);
    } else if (std::strncmp(p, "flower_fault:", 13) == 0) {
      flower_fault = std::strtol(p + 13, nullptr, 10);
    }
    p = std::strchr(p, ';');
    if (p) ++p;
  }

  // Update connection label
  if (!fconnected) {
    m_conn_label->setText(tr("NOT CONNECTED"));
    m_conn_label->setStyleSheet("color: red; font-weight: bold;");
    setControlsEnabled(false);
    m_stop_button->setEnabled(false);
    m_pos_label->setText(tr("—"));
    m_homed_label->setText(tr("No"));
    m_moving_label->setText(tr("Idle"));
    m_fault_label->setText(tr("—"));
    m_fault_label->setStyleSheet("");
    return;
  }
  m_conn_label->setText(tr("OK"));
  m_conn_label->setStyleSheet("color: green;");

  // Update UI labels
  if (fmoving) {
    m_pos_label->setText(tr("Moving…"));
    m_moving_label->setText(tr("Moving"));
  } else {
    if (fpos < 0)
      m_pos_label->setText(tr("Unknown"));
    else
      m_pos_label->setText(
          QString("%1 — %2").arg(fpos).arg(m_filter_names[fpos]->text()));
    m_moving_label->setText(tr("Idle"));
  }
  m_homed_label->setText(fhomed ? tr("Yes") : tr("No"));
  m_upper_qc_label->setText(QString::number(fupper_qc));
  m_lower_qc_label->setText(QString::number(flower_qc));

  // Fault state
  if (fupper_fault && flower_fault) {
    m_fault_label->setText(tr("Both drives"));
    m_fault_label->setStyleSheet("color: red; font-weight: bold;");
  } else if (fupper_fault) {
    m_fault_label->setText(tr("Upper"));
    m_fault_label->setStyleSheet("color: red; font-weight: bold;");
  } else if (flower_fault) {
    m_fault_label->setText(tr("Lower"));
    m_fault_label->setStyleSheet("color: red; font-weight: bold;");
  } else {
    m_fault_label->setText(tr("None"));
    m_fault_label->setStyleSheet("");
  }

  // Stop is always available when the hardware is connected.
  m_stop_button->setEnabled(true);
  // Enable other controls only when connected and homed (not moving)
  bool can_move = fhomed && !fmoving;
  m_go_button->setEnabled(can_move);
  m_home_button->setEnabled(!fmoving);
  for (int i = 0; i <= 10; ++i)
    m_slot_buttons[i]->setEnabled(can_move);
}

void FilterWheelTab::update_status_loop() {
  // Socket is owned by WheelThread or ImageThread — skip.
  if (m_busy || m_socket_busy)
    return;
  if (!csock || !*csock) {
    setControlsEnabled(false);
    m_conn_label->setText(tr("No daemon"));
    m_conn_label->setStyleSheet("");
    m_pos_label->setText(tr("—"));
    m_homed_label->setText(tr("No"));
    m_moving_label->setText(tr("Idle"));
    return;
  }

  std::memset(buffer, 0, sizeof(buffer));
  (*csock)->send("wheel status\n");
  int n = (*csock)->recv(buffer, sizeof(buffer) - 1);
  if (n > 0) {
    parseStatusResponse(buffer);
  }
}
