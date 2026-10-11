#include "connectiontab.h"
#include "cliutils.h"
#include "helmos-andor2k/cpp_socket.hpp"
#include <QGroupBox>
#include <QMessageBox>
#include <QRegularExpressionValidator>
#include <QThread>
#include <cstring>
#include "helmos-andor2k/andor2k.hpp"
#include <sys/socket.h>
#include <sys/time.h>

using andor2k::ClientSocket;
using andor2k::Socket;

std::string ghost;
int gport_no;

ConnectionTab::ConnectionTab(ClientSocket *&csocket, QWidget *parent)
    : QWidget(parent) {
#ifdef DEBUG
  printf("[DEBUG][ANDOR2K::client::%15s] Constructing ConnectionTab\n",
         __func__);
#endif

  createGui();
  setLayout(v_main_layout);
  csock = &csocket;
  connect(m_edit_button, SIGNAL(clicked()), this, SLOT(make_editable()));
  connect(m_connect_button, SIGNAL(clicked()), this, SLOT(sock_connect()));
  connect(m_defaults_button, SIGNAL(clicked()), this, SLOT(reset_defaults()));
  connect(m_disconnect_button, SIGNAL(clicked()), this, SLOT(disconnect()));
  connect(m_update_button, SIGNAL(clicked()), this, SLOT(get_status()));
  connect(m_shutdown_button, SIGNAL(clicked()), this, SLOT(shutdown_daemon()));
  connect(m_update_observer_button, SIGNAL(clicked()), this, SLOT(send_observer_update_command()));

#ifdef DEBUG
  printf("[DEBUG][ANDOR2K::client::%15s] ConnectionTab Socket at %p -> %p",
         __func__, &csock, csock);
  if (*csock)
    printf(" -> %p\n", *csock);
  else
    printf(" -> nowhere!\n");
  printf("[DEBUG][ANDOR2K::client::%15s] Finished constructing ConnectionTab\n",
         __func__);
#endif
}

void ConnectionTab::createGui() {

  m_hostname_ledit = new QLineEdit;
  // m_hostname_ledit->setPlaceholderText("Hostname IP");
  m_hostname_ledit->setText("localhost");
  m_hostname_ledit->setToolTip(
      "Hostname where ANDOR2K daemon is running; note that for local "
      "connections you can use either \"127.0.0.1\" or \"localhost\"");

  m_port_ledit = new QLineEdit;
  // m_port_ledit->setPlaceholderText("Listening Port On Host");
  m_port_ledit->setText("8080");
  QRegularExpression rx(
      "\\d{3,5}"); /* only allow integers in range [100-99999] */
  QValidator *validator = new QRegularExpressionValidator(rx, this);
  m_port_ledit->setValidator(validator);
  m_port_ledit->setToolTip("Port to connect on for ANDOR2K daemon");

  m_init_temp = new QLineEdit;
  // m_init_temp->setPlaceholderText("Set initial target temperature (dec C)");
  m_init_temp->setText("-90");
  QRegularExpression rtx("[+-]?[0-9]{1,3}");
  QValidator *tvalidator = new QRegularExpressionValidator(rtx, this);
  m_init_temp->setValidator(tvalidator);
  m_init_temp->setToolTip(
      "Target temperature (in Celsius) fot the camera at startup");

  m_observer = new QLineEdit;
  m_observer->setText(get_username_or("andor2k"));
  m_observer->setToolTip("Observer name; this name will be written to the FITS "
                         "header, if exposures are to be taken");

  m_temp_c = new QLineEdit;
  m_temp_c->setReadOnly(true);
  m_con_status = new QLineEdit;
  m_con_status->setText("not connected");
  m_con_status->setReadOnly(true);
  m_server_info = new QLineEdit;
  m_server_info->setReadOnly(true);
  m_time = new QLineEdit;
  m_time->setReadOnly(true);

  m_connect_button = new QPushButton("Connect");
  m_connect_button->setEnabled(true);

  m_disconnect_button = new QPushButton("Disconnect", this);
  m_disconnect_button->setEnabled(false);

  m_defaults_button = new QPushButton("Reset Defaults");
  m_defaults_button->setEnabled(true);

  m_edit_button = new QPushButton("Edit");
  m_edit_button->setEnabled(true);

  m_shutdown_button = new QPushButton("Shutdown Daemon");
  m_shutdown_button->setEnabled(false);

  m_update_button = new QPushButton("Update Status");
  m_update_button->setEnabled(false);
  
  m_update_observer_button = new QPushButton("Set");
  m_update_observer_button->setEnabled(true);

  // group connection options
  QGroupBox *con_gbox = new QGroupBox(tr("Connection Options"));
  QGridLayout *con_layout = new QGridLayout;
  con_layout->addWidget(new QLabel(tr("Host:")), 0, 0);
  con_layout->addWidget(m_hostname_ledit, 0, 1);
  con_layout->addWidget(new QLabel(tr("Port:")), 1, 0);
  con_layout->addWidget(m_port_ledit, 1, 1);
  con_gbox->setLayout(con_layout);

  // group startup options
  QGroupBox *init_gbox = new QGroupBox(tr("Initialization Options"));
  QGridLayout *init_layout = new QGridLayout;
  // init_layout->addWidget(new QLabel(tr("Target Temperature at StartUp")), 0, 0);
  // init_layout->addWidget(m_init_temp, 0, 1);
  // init_layout->addWidget(new QLabel(tr("deg C.")), 0, 2);
  init_layout->addWidget(new QLabel(tr("Observer Name")), 1, 0);
  init_layout->addWidget(m_observer, 1, 1, 1, 2);
  init_layout->addWidget(m_update_observer_button, 2, 1);
  init_gbox->setLayout(init_layout);

  // group buttons
  h_button_layout = new QHBoxLayout;
  h_button_layout->addWidget(m_connect_button);
  h_button_layout->addWidget(m_disconnect_button);
  h_button_layout->addWidget(m_edit_button);
  h_button_layout->addWidget(m_defaults_button);

  // group server response status
  QGroupBox *server_gbox = new QGroupBox(tr("Server Status"));
  QGridLayout *server_layout = new QGridLayout;
  server_layout->addWidget(new QLabel(tr("Connection Status")), 0, 0, 2, 1);
  server_layout->addWidget(m_con_status, 1, 0, 2, 1);
  server_layout->addWidget(new QLabel(tr("ANDOR2K Temperature (deg C)")), 2, 0,
                           2, 1);
  server_layout->addWidget(m_temp_c, 3, 0, 2, 1);
  server_layout->addWidget(new QLabel(tr("ANDOR2K Status")), 4, 0, 2, 1);
  server_layout->addWidget(m_server_info, 5, 0, 2, 1);
  server_layout->addWidget(new QLabel(tr("Last Response Time")), 6, 0, 2, 1);
  server_layout->addWidget(m_time, 7, 0, 2, 1);
  server_gbox->setLayout(server_layout);

  // right side of panle; contains information (line) edits and update button
  QVBoxLayout *server_side = new QVBoxLayout;
  server_side->addWidget(server_gbox);
  server_side->addWidget(m_update_button);

  // left side of panel; contains connection info, and intitialization defaults
  QVBoxLayout *v_options_layout = new QVBoxLayout;
  // v_options_layout = new QVBoxLayout;
  v_options_layout->addWidget(con_gbox);
  v_options_layout->addWidget(init_gbox);
  v_options_layout->addLayout(h_button_layout);

  // combine right and left sides to a single window
  QHBoxLayout *hb_layout = new QHBoxLayout;
  hb_layout->addLayout(v_options_layout, 1);
  hb_layout->addLayout(server_side, 3);

  // combine window (right/left) and shutdown button
  v_main_layout = new QVBoxLayout;
  v_main_layout->addLayout(hb_layout);
  v_main_layout->addWidget(m_shutdown_button);

  setUnEditable(); // options are uneditable
}

/*
void ConnectionTab::send_settemp() {
  if (*csock == nullptr) {
    QMessageBox msbox(QMessageBox::Critical, "Connection Error",
                      "Cannot send command to daemon! Need to connect first");
    msbox.exec();
    return;
  }

  std::memset(buffer, 0, 1024);
  int target_temp = m_init_temp->text().toInt();
  sprintf(buffer, "settemp %d", target_temp);
  (*csock)->send(buffer);

  setTempThread *tthread = new setTempThread(*csock);
  connect(tthread, &setTempThread::resultReady, this,
          &ConnectionTab::serverJobDone);
  connect(tthread, &setTempThread::newResponseReady, this,
          &ConnectionTab::serverJobUpdate);
  connect(tthread, &setTempThread::finished, tthread, &QObject::deleteLater);
  tthread->start();

  return;
}

void ConnectionTab::serverJobUpdate(const QString &response) {
  QStringList list(split_command(response));
  QString val;
  m_temp_c->setText(get_val("temp", list));
  m_server_info->setText(get_val("status", list));
  m_time->setText(get_val("time", list));
  return;
}

void ConnectionTab::serverJobDone() {
  m_connect_button->setEnabled(false);
  m_disconnect_button->setEnabled(true);
  m_shutdown_button->setEnabled(true);
  m_update_button->setEnabled(true);
}
*/

int ConnectionTab::make_observername_command(char *buffer) {
  auto qsname = m_observer->text();
  std::string sname = qsname.toStdString();
  if (sname.size() >= 31) {
      QMessageBox msbox(QMessageBox::Critical, "Parameter Error",
                        "Observer Name can have a maximum of 31 characters length");
      m_observer->setText(QString("andor2k"));
      msbox.exec();
      return 1;
  }

  // char buffer[MAX_SOCKET_BUFFER_SIZE];
  std::memset(buffer, 0, MAX_SOCKET_BUFFER_SIZE);
  int sz = std::sprintf(buffer, "setparam observername=%s", sname.c_str());
  return !(sz > 0);
}

void ConnectionTab::send_observer_update_command() {
  // check that the connection/socket is alive
  if (*csock == nullptr) {
    QMessageBox msbox(QMessageBox::Critical, "Connection Error",
                      "Cannot send command to daemon! Need to connect first");
    msbox.exec();
    return;
  }

  // write the command to be sent to the buffer
  char buffer[MAX_SOCKET_BUFFER_SIZE];
  if (make_observername_command(buffer)) return;

  // send command to deamon
#ifdef DEBUG
  printf("[DEBUG][ANDOR2K::client::%15s] sending command: \"%s\"\n", __func__,
         buffer);
#endif
  printf("--> parameter command: [%s] <--\n", buffer);
  (*csock)->send(buffer);

  return;
}

void ConnectionTab::setEditable() {
  m_hostname_ledit->setEnabled(true);
  m_port_ledit->setEnabled(true);
  m_init_temp->setEnabled(true);
  // m_observer->setEnabled(true);
  m_update_button->setEnabled(true);
}

void ConnectionTab::setUnEditable() {
  m_hostname_ledit->setEnabled(false);
  m_port_ledit->setEnabled(false);
  m_init_temp->setEnabled(false);
  // m_observer->setEnabled(false);
  // m_update_button->setEnabled(false);
}

void ConnectionTab::get_status() {

  // waiting for an answer of type:
  // 'status:IDLE; waiting for instructions;temp: +18 (Temperature is off);time:2021-10-05 12:54:01;'
  
  if (*csock == nullptr) {
    QMessageBox msbox(QMessageBox::Critical, "Connection Error",
                      "Cannot send command to daemon! Need to connect first");
    msbox.exec();
    return;
  }
  
  // send 'status' command and resolve back answer
  std::memset(buffer, 0, 1024);
  std::strcpy(buffer, "status");
  if ((*csock)->send(buffer)<1) {
#ifdef DEBUG
    printf(">> failed to send status command to deamon!\n");
#endif
    return;
  }
#ifdef DEBUG
  printf(">> client sent command %s\n", buffer);
#endif
  
  std::memset(buffer, 0, 1024);
  if ((*csock)->recv(buffer, 1024)<1) {
#ifdef DEBUG
    printf(">> failed to get back answer from deamon for status\n");
#endif
    return;
  }
#ifdef DEBUG
  printf(">> client received answer [%s]\n", buffer);
#endif

  QString val(64, ' '), empty(" ");
  QStringList list(split_command(buffer));
  val = get_val("status", list);
  if (val != empty) {
    m_server_info->setText(val);
    m_server_info->setStyleSheet("QLabel { color: black; }");
  } else {
    m_server_info->setStyleSheet("QLabel { color: grey; }");
  }
  
  val = get_val("temp", list);
  if (val != empty) {
    m_temp_c->setText(val);
    m_temp_c->setStyleSheet("QLabel { color: black; }");
  } else {
    m_temp_c->setStyleSheet("QLabel { color: grey; }");
  }
  
  val = get_val("time", list);
  if (val != empty) {
    m_time->setText(val);
    m_time->setStyleSheet("QLabel { color: black; }");
  } else {
    m_time->setStyleSheet("QLabel { color: grey; }");
  }

  return;
}

void ConnectionTab::sock_connect() {
  setUnEditable();

  if (*csock) {
    QMessageBox msbox(QMessageBox::Critical, "Connection Error",
                      "Already Connected!");
    msbox.exec();
    return;
  }

  std::string host = m_hostname_ledit->text().toStdString();
  try {
    *csock = new ClientSocket(host.c_str(), m_port_ledit->text().toInt());
    // Set a recv timeout so blocking recv() calls in the main thread (e.g.
    // update_status_loop) cannot freeze the UI indefinitely if the daemon
    // stops responding. WheelThread handles EAGAIN by looping (see below).
    {
      struct timeval tv{5, 0};  // 5-second timeout
      setsockopt((*csock)->sockid(), SOL_SOCKET, SO_RCVTIMEO,
                 &tv, sizeof(tv));
    }
    ghost = host;
    gport_no = m_port_ledit->text().toInt();
  } catch (std::exception &e) {
    *csock = nullptr;
    QMessageBox messageBox(QMessageBox::Critical, "Connection Error",
                           "Could not connect to daemon!");
    messageBox.exec();
    return;
  }
  
  // get the status, aka send 'status' and resolve answer
  get_status();

  m_connect_button->setEnabled(false);
  m_disconnect_button->setEnabled(true);
  m_shutdown_button->setEnabled(true);
  m_update_button->setEnabled(true);
  m_con_status->setText("connected");

  // send_settemp(); RUN

#ifdef DEBUG
  printf("[DEBUG][ANDOR2K::client::%15s] Socket points to: ", __func__);
  if (*csock) {
    printf("%p -> %p\n", csock, *csock);
  } else {
    printf(" nowhere!\n");
  }
#endif
}

void ConnectionTab::reset_defaults() {
  m_hostname_ledit->setText("localhost");
  m_port_ledit->setText("8080");
  m_init_temp->setText("-90");
  m_observer->setText(get_username_or("andor2k"));
  // m_defaults_button->setEnabled(false);
  setUnEditable();
}

void ConnectionTab::disconnect() {
  (*csock)->close_socket();
  delete (*csock);
  *csock = nullptr;
  m_connect_button->setEnabled(true);
  m_disconnect_button->setEnabled(false);
  m_shutdown_button->setEnabled(false);
  m_update_button->setEnabled(false);
  m_con_status->setText("not connected");
}

void ConnectionTab::shutdown_daemon() {
  char buffer[1024];
  std::memset(buffer, 0, 1024);
  std::strcpy(buffer, "shutdown");
  (*csock)->send(buffer);
  disconnect();
  m_con_status->setText("not connected");
}

void ConnectionTab::make_editable() { setEditable(); }
