#ifndef FILTERWHEELTAB_H
#define FILTERWHEELTAB_H

#include "helmos-andor2k/cpp_socket.hpp"
#include "wheelthread.h"
#include <QButtonGroup>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QTimer>
#include <QWidget>

class FilterWheelTab : public QWidget {
  Q_OBJECT
public:
  explicit FilterWheelTab(andor2k::ClientSocket *&sock,
                          QWidget *parent = nullptr);

private:
  void createGui();
  void setControlsEnabled(bool enabled);
  void parseStatusResponse(const char *response);
  void startWheelCommand(const char *cmd); // spawns WheelThread, stops timer

  andor2k::ClientSocket **csock;
  char buffer[1024];
  bool m_busy = false;        // true while WheelThread owns the socket
  bool m_socket_busy = false; // true while ExposureTab's ImageThread owns socket

  // For determinate progress bar when daemon provides fmove_qc.
  long m_move_qc = 0;       // total QC distance for this move (0 = indeterminate)
  long m_start_upper_qc = 0;
  long m_start_lower_qc = 0;

  // Left panel — slot selection
  QButtonGroup *m_slot_group;
  QRadioButton *m_slot_buttons[11];
  QLineEdit    *m_filter_names[11];  // user-editable, persisted via QSettings

  // Right panel — status
  QLabel *m_conn_label;    // hardware connection state
  QLabel *m_pos_label;
  QLabel *m_homed_label;
  QLabel *m_moving_label;
  QLabel *m_upper_qc_label;
  QLabel *m_lower_qc_label;
  QProgressBar *m_progress; // indeterminate, shown while move is in flight

  // Control buttons
  QPushButton *m_home_button;
  QPushButton *m_stop_button;
  QPushButton *m_go_button;

  // 1-second poll timer
  QTimer *m_timer;

private slots:
  void move_to_slot();
  void go_home();
  void stop_motion();
  void update_status_loop();
  void onCommandDone(const QString &result);
  void onWheelProgress(const QString &update);

protected:
  void showEvent(QShowEvent *event) override; // start poll timer when tab becomes visible
  void hideEvent(QHideEvent *event) override; // stop poll timer when tab hidden

public slots:
  // Emitted true when a wheel command starts, false when it finishes.
  // Consumed by ExposureTab to disable its Start button while wheel moves.
  void setSocketBusy(bool busy);

signals:
  void wheelBusy(bool);
};

#endif // FILTERWHEELTAB_H
