#include "exposuretab.h"
#include "cliutils.h"
#include "imagethread.h"
#include <QApplication>
#include <QGroupBox>
#include <QMessageBox>
#include <QStringList>
#include <cstring>

using andor2k::ClientSocket;
using andor2k::Socket;

extern std::string ghost;
extern int gport_no;

ExposureTab::ExposureTab(ClientSocket *&csocket, QWidget *parent)
    : QWidget(parent) {
  createGui();
  setLayout(main_layout);
  csock = &csocket;

  connect(m_start_button, SIGNAL(clicked()), this, SLOT(set_exposure()));
  
  connect(m_clear_button, SIGNAL(clicked()), this, SLOT(clearlogs()));
  
  connect(m_cancel_button, SIGNAL(clicked()), this, SLOT(send_abort()));

  connect(m_type_cbox, QOverload<int>::of(&QComboBox::currentIndexChanged),
          [=](int index) {
            if (index == 2)
              this->m_exposure_ledit->setEnabled(false);
            else
              this->m_exposure_ledit->setEnabled(true);
          });

  connect(m_tel_cb, &QCheckBox::clicked, [=](bool checked) {
    if (checked)
      m_tel_tries->setEnabled(true);
    else
      m_tel_tries->setEnabled(false);
  });
}

void ExposureTab::send_abort() {
  // Do you really want to quit ? ....
  QMessageBox::StandardButton reply;
  reply = QMessageBox::question(this, "Abort Exposure Confirmation", "Are you sure you want to abort exposure ?",
                                QMessageBox::Yes|QMessageBox::No);
  // no-opt, user does not want to quit!
  if (reply == QMessageBox::No) return;

  // try opening a new socket, at theoretically a listening port to send 
  // command
  printf(">> trying to send abort signal\n");
  int aport = gport_no + 1;
  try {
    ClientSocket abort_sock(ghost.c_str(), aport);
    printf(">> connected to %s:%d\n", ghost.c_str(), aport);
    char buf[32] = {'\0'};
    std::strcpy(buf, "abort");
    printf("---> sending abort signal <---\n");
    if (abort_sock.send(buffer)<1)
      printf(">> failed sending abort signal to server!\n");
  } catch (std::exception &e) {
    char erbuf[128];
    std::sprintf(erbuf, "Could not connect to abort listening port %s:%d", ghost.c_str(), aport);
    QMessageBox messageBox(QMessageBox::Critical, "Connection Error", QString(erbuf));
    messageBox.exec();
    return;
  }
  
  return;
}

void ExposureTab::set_exposure() {
  // clear previous logs (if any)
  clearlogs();

  // check that the connection/socket is alive
  if (*csock == nullptr) {
    QMessageBox msbox(QMessageBox::Critical, "Connection Error",
                      "Cannot send command to daemon! Need to connect first");
    msbox.exec();
    return;
  }

  if (this->make_command(this->buffer))
    return;

  // send command to deamon
#ifdef DEBUG
  printf("[DEBUG][ANDOR2K::client::%15s] sending command: \"%s\"\n", __func__,
         buffer);
#endif
  (*csock)->send(buffer);

  // we should now be able to click the abort button
  m_cancel_button->setEnabled(true);

  // start listening thread for server info
  ImageThread *ithread = new ImageThread(*csock, this);

  connect(ithread, &ImageThread::resultReady, this,
          &ExposureTab::serverJobDone);
  connect(ithread, &ImageThread::newResponseReady, this,
          &ExposureTab::serverJobUpdate);
  connect(ithread, &ImageThread::finished, ithread, &QObject::deleteLater);

  ithread->start();

  // Pause the FilterWheelTab status-poll timer for the duration of the
  // exposure. Without this, the timer fires on the main thread and calls
  // recv() on the shared socket while ImageThread is also calling recv(),
  // causing a race that corrupts the message stream and hangs the GUI.
  emit socketBusy(true);

  // m_cancel_button->setEnabled(false);
  m_clear_button->setEnabled(false);
  return;
}

void ExposureTab::serverJobUpdate(const QString &response) {
#ifdef DEBUG
  printf("--> got string from server: [%s] <---\n",
         response.toStdString().c_str());
#endif
  QStringList list(split_command(response));
  QString val(64, ' '), empty(" ");
  bool converted;

  val = get_val("info", list);
  if (val != empty) {
    m_server_info->setText(val);
    m_server_info->setStyleSheet("QLabel { color: black; }");
  } else {
    m_server_info->setStyleSheet("QLabel { color: grey; }");
  }

  val = get_val("status", list);
  if (val != empty) {
    m_server_status->setText(val);
    m_server_status->setStyleSheet("QLabel { color: black; }");
  } else {
    m_server_status->setStyleSheet("QLabel { color: grey; }");
  }

  val = get_val("time", list);
  if (val != empty) {
    m_server_time->setText(val);
    m_server_time->setStyleSheet("QLabel { color: black; }");
  } else {
    m_server_time->setStyleSheet("QLabel { color: grey; }");
  }

  val = get_val("image", list);
  if (val != empty) {
    m_image_nr->setText(val);
    m_image_nr->setStyleSheet("QLabel { color: black; }");
  } else {
    m_image_nr->setStyleSheet("QLabel { color: grey; }");
  }

  val = get_val("elapsedt", list);
  if (val != empty) {
    m_elt->setText(val);
    m_elt->setStyleSheet("QLabel { color: black; }");
  } else {
    m_elt->setStyleSheet("QLabel { color: grey; }");
  }

  val = get_val("selapsedt", list);
  if (val != empty) {
    m_series_elt->setText(val);
    m_series_elt->setStyleSheet("QLabel { color: black; }");
  } else {
    m_series_elt->setStyleSheet("QLabel { color: grey; }");
  }

  int percent = get_val("progperc", list).toInt(&converted);
  if (converted)
    m_prog_bar->setValue(percent > 100 ? 100 : percent);

  percent = get_val("sprogperc", list).toInt(&converted);
  if (converted)
    m_series_prog_bar->setValue(percent > 100 ? 100 : percent);

  return;
}

void ExposureTab::serverJobDone(const QString &response) {
  serverJobUpdate(response);

  // get the exit status
  QStringList list(split_command(response));
  int error = list_has_error(list);
  if (!error) {
    QMessageBox msbox(QMessageBox::Information, "Exposure Status",
                      "All done! Exposure ended");
    m_prog_bar->setValue(100);
    m_series_prog_bar->setValue(100);
    msbox.exec();
  } else {
    QString info("Exposure ended (status:");
    info += get_val("error", list);
    info += ")\n";
    info += get_val("status", list);
    QMessageBox msbox(QMessageBox::Critical, "Exposure Status", info);
    msbox.exec();
  }

  setEditable(true);
  emit socketBusy(false); // allow FilterWheelTab timer to resume
}

void ExposureTab::setWheelBusy(bool busy) {
  m_start_button->setEnabled(!busy);
}

int ExposureTab::make_command(char *buffer) {
  std::size_t idx = 0;
  std::memset(buffer, '\0', 1024);
  std::strcpy(buffer + idx, "image ");
  idx = std::strlen(buffer);

  /* check for empty edits */
  char error_msg[128];
  std::memset(error_msg, 0, 128);
  int error = 0;
  if (m_filename_ledit->text().isEmpty()) {
    error = 1;
    std::strcpy(error_msg, "Image filename cannot be empty!");
  } else if (m_exposure_ledit->text().isEmpty()) {
    error = 1;
    std::strcpy(error_msg, "Exposure cannot be empty!");
  } else if (m_obj_name->text().isEmpty()) {
    error = 1;
    std::strcpy(error_msg, "Object name cannot be empty!");
  } else if (m_filter_name->text().isEmpty()) {
    error = 1;
    std::strcpy(error_msg, "Filter name cannot be empty!");
  } else if (m_nimages_ledit->text().isEmpty()) {
    error = 1;
    std::strcpy(error_msg, "Number of Images cannot be empty!");
  } else if (m_vbin_ledit->text().isEmpty() || m_hbin_ledit->text().isEmpty()) {
    error = 1;
    std::strcpy(error_msg, "Vertical/Horizontal binning cannot be empty!");
  } else if (m_vstart_pix->text().isEmpty() || m_vend_pix->text().isEmpty()) {
    error = 1;
    std::strcpy(error_msg, "Vertical start/end pixels cannot be empty!");
  } else if (m_hstart_pix->text().isEmpty() || m_hend_pix->text().isEmpty()) {
    error = 1;
    std::strcpy(error_msg, "Horizontal start/end pixels cannot be empty!");
  }
  if (error) {
    QMessageBox msbox(QMessageBox::Critical, "Exposure Options Error",
                      error_msg);
    msbox.exec();
    return error;
  }

  /* filename */
  std::strcpy(buffer + idx, "--filename ");
  idx = std::strlen(buffer);
  auto tval = m_filename_ledit->text().toStdString();
  std::strcpy(buffer + idx, tval.c_str());
  idx = std::strlen(buffer);
  buffer[idx] = ' ';
  ++idx;

  /* exposure (if disabled, set to 0) */
  std::strcpy(buffer + idx, "--exposure ");
  idx = std::strlen(buffer);
  if (!m_exposure_ledit->isEnabled()) { /* aka bias */
    tval = std::string("0.0");
  } else {
    tval = m_exposure_ledit->text().toStdString();
  }
  std::strcpy(buffer + idx, tval.c_str());
  idx = std::strlen(buffer);
  buffer[idx] = ' ';
  ++idx;

  /* nimages */
  std::strcpy(buffer + idx, "--nimages ");
  idx = std::strlen(buffer);
  tval = m_nimages_ledit->text().toStdString();
  std::strcpy(buffer + idx, tval.c_str());
  idx = std::strlen(buffer);
  buffer[idx] = ' ';
  ++idx;

  /* vbin */
  std::strcpy(buffer + idx, "--vbin ");
  idx = std::strlen(buffer);
  tval = m_vbin_ledit->text().toStdString();
  std::strcpy(buffer + idx, tval.c_str());
  idx = std::strlen(buffer);
  buffer[idx] = ' ';
  ++idx;

  /* hbin */
  std::strcpy(buffer + idx, "--hbin ");
  idx = std::strlen(buffer);
  tval = m_hbin_ledit->text().toStdString();
  std::strcpy(buffer + idx, tval.c_str());
  idx = std::strlen(buffer);
  buffer[idx] = ' ';
  ++idx;

  /* vstart */
  std::strcpy(buffer + idx, "--vstart ");
  idx = std::strlen(buffer);
  tval = m_vstart_pix->text().toStdString();
  std::strcpy(buffer + idx, tval.c_str());
  idx = std::strlen(buffer);
  buffer[idx] = ' ';
  ++idx;

  /* vend */
  std::strcpy(buffer + idx, "--vend ");
  idx = std::strlen(buffer);
  tval = m_vend_pix->text().toStdString();
  std::strcpy(buffer + idx, tval.c_str());
  idx = std::strlen(buffer);
  buffer[idx] = ' ';
  ++idx;

  /* hstart */
  std::strcpy(buffer + idx, "--hstart ");
  idx = std::strlen(buffer);
  tval = m_hstart_pix->text().toStdString();
  std::strcpy(buffer + idx, tval.c_str());
  idx = std::strlen(buffer);
  buffer[idx] = ' ';
  ++idx;

  /* hend */
  std::strcpy(buffer + idx, "--hend ");
  idx = std::strlen(buffer);
  tval = m_hend_pix->text().toStdString();
  std::strcpy(buffer + idx, tval.c_str());
  idx = std::strlen(buffer);
  buffer[idx] = ' ';
  ++idx;

  /* type */
  std::strcpy(buffer + idx, "--type ");
  idx = std::strlen(buffer);
  tval = m_type_cbox->currentText().toStdString();
  std::strcpy(buffer + idx, tval.c_str());
  idx = std::strlen(buffer);
  buffer[idx] = ' ';
  ++idx;

  /* object */
  std::strcpy(buffer + idx, "--object ");
  idx = std::strlen(buffer);
  tval = m_obj_name->text().toStdString();
  std::strcpy(buffer + idx, tval.c_str());
  idx = std::strlen(buffer);
  buffer[idx] = ' ';
  ++idx;

  /* filter */
  std::strcpy(buffer + idx, "--filter ");
  idx = std::strlen(buffer);
  tval = m_filter_name->text().toStdString();
  std::strcpy(buffer + idx, tval.c_str());
  idx = std::strlen(buffer);
  buffer[idx] = ' ';
  ++idx;

  /* aristarchos headers */
  std::strcpy(buffer + idx, "--ar-tries ");
  idx = std::strlen(buffer);
  if (m_tel_tries->isEnabled()) {
    tval = m_tel_tries->text().toStdString();
  } else {
    tval = std::string("0");
  }
  std::strcpy(buffer + idx, tval.c_str());
  idx = std::strlen(buffer);

#ifdef DEBUG
  //printf("[DEBUG][ANDOR2K::client::%15s] Sending command to daemon: [%s]\n",
  //__func__, buffer);
  printf("[DEBUG][ANDOR2K::client::%15s] Command size: %d\n", __func__,
         (int)std::strlen(buffer));
#endif
  return 0;
}

void ExposureTab::setEditable(bool editable) {
  m_filename_ledit->setEnabled(editable);
  if (m_type_cbox->currentIndex() == 2) {
    m_exposure_ledit->setEnabled(false);
  } else {
    m_exposure_ledit->setEnabled(editable);
  }
  m_nimages_ledit->setEnabled(editable);
  m_vbin_ledit->setEnabled(editable);
  m_hbin_ledit->setEnabled(editable);
  m_vstart_pix->setEnabled(editable);
  m_hstart_pix->setEnabled(editable);
  m_vend_pix->setEnabled(editable);
  m_hend_pix->setEnabled(editable);
  m_obj_name->setEnabled(editable);
  m_filter_name->setEnabled(editable);
}

void ExposureTab::clearlogs() {
  m_server_info->setText("");
  m_server_status->setText("");
  m_server_time->setText("");
  m_image_nr->setText("");
  m_elt->setText("");
  m_series_elt->setText("");
  m_prog_bar->setValue(0);
  m_series_prog_bar->setValue(0);
}

void ExposureTab::createGui() {
  m_filename_ledit = new QLineEdit;
  m_exposure_ledit = new QLineEdit;
  m_nimages_ledit = new QLineEdit;
  m_vbin_ledit = new QLineEdit;
  m_hbin_ledit = new QLineEdit;
  m_vstart_pix = new QLineEdit;
  m_hstart_pix = new QLineEdit;
  m_vend_pix = new QLineEdit;
  m_hend_pix = new QLineEdit;
  m_obj_name = new QLineEdit;
  m_filter_name = new QLineEdit;

  // options for NumImages edit
  QRegularExpression rx_ni("[0-9]{1,3}");
  QValidator *validator_ni = new QRegularExpressionValidator(rx_ni, this);
  m_nimages_ledit->setValidator(validator_ni);
  m_nimages_ledit->setToolTip(
      "Number of images/exposures to be performed in run.");
  m_nimages_ledit->setText("1");

  // options for Filename edit
  QRegularExpression rx_fn(
      "[a-zA-Z0-9.]{1,127}"); /* only allow integers in range [-999, 999] */
  QValidator *validator_fn = new QRegularExpressionValidator(rx_fn, this);
  m_filename_ledit->setValidator(validator_fn);
  m_filename_ledit->setToolTip(
      "Type in the filename of the exposure(s).\nDo not include path or the "
      "\".fits\" extension\nNote that the filename typed in here, will be "
      "augmented by the\ndate string (aka \"YYYYMMDD\" and a serial number "
      "starting from 1");
  m_filename_ledit->setText("img");

  // options for Exposure edit
  QRegularExpression rx_ex(
      "[0-9.]{1,10}"); /* only allow integers in range [-999, 999] */
  QValidator *validator_ex = new QRegularExpressionValidator(rx_ex, this);
  m_exposure_ledit->setValidator(validator_ex);
  m_exposure_ledit->setToolTip("Exposure time in seconds, as float value.");
  m_exposure_ledit->setText("0.5");

  // options for Vertical Binning edit
  QRegularExpression rx_vbn(
      "[0-9]{1,3}"); /* only allow integers in range [-999, 999] */
  QValidator *validator_vbn = new QRegularExpressionValidator(rx_vbn, this);
  m_vbin_ledit->setValidator(validator_vbn);
  m_vbin_ledit->setToolTip("Vertical binning, as integer value.");
  m_vbin_ledit->setText("2");

  // options for Horizontal Binning edit
  QRegularExpression rx_hbn(
      "[0-9]{1,3}"); /* only allow integers in range [-999, 999] */
  QValidator *validator_hbn = new QRegularExpressionValidator(rx_hbn, this);
  m_hbin_ledit->setValidator(validator_hbn);
  m_hbin_ledit->setToolTip("Horizontal binning, as integer value.");
  m_hbin_ledit->setText("2");

  // options for Vertical Start Pixel edit
  QRegularExpression rx_vstart(
      "[0-9]{1,4}"); /* only allow integers in range [-999, 999] */
  QValidator *validator_vstart =
      new QRegularExpressionValidator(rx_vstart, this);
  m_vstart_pix->setValidator(validator_vstart);
  m_vstart_pix->setToolTip(
      "Vertical start pixel (inclusive); integer in range [1, 2048]");
  m_vstart_pix->setText("1");

  // options for Vertical End Pixel edit
  QRegularExpression rx_vend(
      "[0-9]{1,4}"); /* only allow integers in range [-999, 999] */
  QValidator *validator_vend = new QRegularExpressionValidator(rx_vend, this);
  m_vend_pix->setValidator(validator_vend);
  m_vend_pix->setToolTip(
      "Vertical end pixel (inclusive); integer in range [1, 2048]");
  m_vend_pix->setText("2048");

  // options for Horizontal Start Pixel edit
  QRegularExpression rx_hstart(
      "[0-9]{1,4}"); /* only allow integers in range [-999, 999] */
  QValidator *validator_hstart =
      new QRegularExpressionValidator(rx_hstart, this);
  m_hstart_pix->setValidator(validator_hstart);
  m_hstart_pix->setToolTip(
      "Horizontal start pixel (inclusive); integer in range [1, 2048]");
  m_hstart_pix->setText("1");

  // options for Horizontal End Pixel edit
  QRegularExpression rx_hend(
      "[0-9]{1,4}"); /* only allow integers in range [-999, 999] */
  QValidator *validator_hend = new QRegularExpressionValidator(rx_hend, this);
  m_hend_pix->setValidator(validator_hend);
  m_hend_pix->setToolTip(
      "Horizontal end pixel (inclusive); integer in range [1, 2048]");
  m_hend_pix->setText("2048");

  m_obj_name->setToolTip("Name of object (will be written in FITS header)");
  m_obj_name->setMaxLength(31);
  m_filter_name->setToolTip("Name of filter applied for the exposure(s)");
  m_filter_name->setMaxLength(15);

  // options for aristarchos header fetch tries
  QRegularExpression rx_telt("[0-9]{1,2}");
  QValidator *validator_telt = new QRegularExpressionValidator(rx_telt, this);
  m_tel_tries = new QLineEdit;
  m_tel_tries->setValidator(validator_telt);
  m_tel_tries->setToolTip(
      "Number of tries to fetch Aristarchos headers begore giving up");
  m_tel_tries->setText("3");
  m_tel_tries->setEnabled(false);

  // checkbox for fetching (or not) aristarchos headers
  m_tel_cb = new QCheckBox("Fetch Aristarchos Headers", this);
  m_tel_cb->setChecked(/*false*/true);
  m_tel_cb->setLayoutDirection(Qt::RightToLeft);

  // Options for Type edit
  QStringList types = {"flat", "object", "bias", "dark"};
  m_type_cbox = new QComboBox(this);
  m_type_cbox->addItems(types);

  // Server Response Panel
  m_server_info = new QLineEdit;
  m_server_status = new QLineEdit;
  m_server_time = new QLineEdit;
  m_image_nr = new QLineEdit;
  m_elt = new QLineEdit;
  m_series_elt = new QLineEdit;

  m_label = new QLabel;
  m_label->setFrameStyle(QFrame::Box | QFrame::Plain);

  // Buttons
  m_cancel_button = new QPushButton("Abort", this);
  m_start_button = new QPushButton("Start", this);
  m_clear_button = new QPushButton("Clear", this);
  m_start_button->setEnabled(true);
  m_cancel_button->setEnabled(false);
  m_clear_button->setEnabled(false);

  // the progress bars
  m_prog_bar = new QProgressBar;
  m_prog_bar->setRange(0, 100);
  m_series_prog_bar = new QProgressBar;
  m_series_prog_bar->setRange(0, 100);

  // group server response
  QGroupBox *server_gbox = new QGroupBox(tr("Server Status"));
  QGridLayout *server_layout = new QGridLayout;
  server_layout->addWidget(new QLabel(tr("ANDOR2K Info")), 0, 0);
  server_layout->addWidget(m_server_info, 0, 1, 1, 2);
  server_layout->addWidget(new QLabel(tr("ANDOR2K Status")), 1, 0);
  server_layout->addWidget(m_server_status, 1, 1, 1, 2);
  server_layout->addWidget(new QLabel(tr("Last Response Time")), 2, 0);
  server_layout->addWidget(m_server_time, 2, 1, 1, 2);
  server_layout->addWidget(new QLabel(tr("Image nr")), 3, 0, 1, 2);
  server_layout->addWidget(m_image_nr, 3, 1);
  server_layout->addWidget(new QLabel(tr("Elapsed Time (image)")), 4, 0);
  server_layout->addWidget(m_elt, 4, 1);
  server_layout->addWidget(new QLabel(tr("sec")), 4, 2);
  server_layout->addWidget(new QLabel(tr("Elapsed Time (series)")), 5, 0);
  server_layout->addWidget(m_series_elt, 5, 1);
  server_layout->addWidget(new QLabel(tr("sec")), 5, 2);
  server_layout->addWidget(new QLabel(tr("Current Exposure Progress")), 6, 0, 1,
                           2);
  server_layout->addWidget(m_prog_bar, 7, 0, 2, 3);
  server_layout->addWidget(new QLabel(tr("Series Exposure Progress")), 9, 0, 1,
                           2);
  server_layout->addWidget(m_series_prog_bar, 10, 0, 2, 3);
  server_layout->addWidget(m_clear_button, 11, 0, 1, 3);
  server_gbox->setLayout(server_layout);

  // group binning options 
  QGroupBox *bin_gbox = new QGroupBox(tr("Binning Options"));
  QGridLayout *bin_layout = new QGridLayout;
  bin_layout->addWidget(new QLabel(tr("Vertical")), 0, 0);
  bin_layout->addWidget(m_vbin_ledit, 0, 1);
  bin_layout->addWidget(new QLabel(tr("Horizontal")), 1, 0);
  bin_layout->addWidget(m_hbin_ledit, 1, 1);
  bin_gbox->setLayout(bin_layout);

  // group image dimensions options
  QGroupBox *pix_gbox = new QGroupBox(tr("Image Dimensions"));
  QGridLayout *pix_layout = new QGridLayout;
  pix_layout->addWidget(new QLabel(tr("Vertical")), 0, 1);
  pix_layout->addWidget(new QLabel(tr("Start")), 0, 2);
  pix_layout->addWidget(m_vstart_pix, 0, 3);
  pix_layout->addWidget(new QLabel(tr("End")), 0, 4);
  pix_layout->addWidget(m_vend_pix, 0, 5);
  pix_layout->addWidget(new QLabel(tr("Horizontal")), 1, 1);
  pix_layout->addWidget(new QLabel(tr("Start")), 1, 2);
  pix_layout->addWidget(m_hstart_pix, 1, 3);
  pix_layout->addWidget(new QLabel(tr("End")), 1, 4);
  pix_layout->addWidget(m_hend_pix, 1, 5);
  pix_gbox->setLayout(pix_layout);

  // group general options
  QGroupBox *gen_gbox = new QGroupBox(tr("General Options"));
  QGridLayout *gen_layout = new QGridLayout;
  gen_layout->addWidget(new QLabel(tr("Filename:")), 0, 0);
  gen_layout->addWidget(m_filename_ledit, 0, 1);
  gen_layout->addWidget(new QLabel(tr("Object Name:")), 1, 0);
  gen_layout->addWidget(m_obj_name, 1, 1);
  gen_layout->addWidget(new QLabel(tr("Type:")), 2, 0);
  gen_layout->addWidget(m_type_cbox, 2, 1);
  gen_layout->addWidget(new QLabel(tr("Exposure (sec):")), 3, 0);
  gen_layout->addWidget(m_exposure_ledit, 3, 1);
  gen_layout->addWidget(new QLabel(tr("Num Images:")), 4, 0);
  gen_layout->addWidget(m_nimages_ledit, 4, 1);
  gen_layout->addWidget(new QLabel(tr("Filter Name:")), 5, 0);
  gen_layout->addWidget(m_filter_name, 5, 1);
  gen_gbox->setLayout(gen_layout);

  // group aristarchos options
  QGroupBox *tel_gbox = new QGroupBox(tr("Aristarchos Options"));
  QGridLayout *tel_layout = new QGridLayout;
  tel_layout->addWidget(m_tel_cb, 0, 0);
  tel_layout->addWidget(new QLabel(tr("Num. Tries")), 0, 1);
  tel_layout->addWidget(m_tel_tries, 0, 2);
  tel_gbox->setLayout(tel_layout);

  // group buttons
  QHBoxLayout *btn_hbox = new QHBoxLayout;
  btn_hbox->addWidget(m_start_button);
  btn_hbox->addWidget(m_cancel_button);

  // QFrame *line = new QFrame();
  // line->setFrameShape(QFrame::HLine);
  // line->setFrameShadow(QFrame::Sunken);

  QVBoxLayout *v_layout = new QVBoxLayout;
  v_layout->addWidget(gen_gbox);
  v_layout->addWidget(bin_gbox);
  v_layout->addWidget(pix_gbox);
  v_layout->addWidget(tel_gbox);
  v_layout->addLayout(btn_hbox);

  main_layout = new QHBoxLayout;
  main_layout->addLayout(v_layout);
  main_layout->addWidget(server_gbox);
}
