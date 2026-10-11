#include "tabdialog.h"
#include "advancedsettings.h"
#include "connectiontab.h"
#include "exposuretab.h"
#include "filterwheeltab.h"
#include <QVBoxLayout>

TabDialog::TabDialog(andor2k::ClientSocket *&sock_pt, QWidget *parent)
    : QDialog(parent), csock(&sock_pt) {

  printf("[DEBUG][ANDOR2K::client::%15s] Constructing TabDialog\n", __func__);
  // csock = &sock_pt;

  m_tabWidget = new QTabWidget;
  m_tabWidget->addTab(new ConnectionTab(sock_pt), tr("Connection"));

  ExposureTab  *expTab = new ExposureTab(sock_pt);
  FilterWheelTab *fwTab = new FilterWheelTab(sock_pt);

  // Stop FilterWheelTab's 1-second status-poll timer while an exposure is
  // running. Both tabs share the same TCP socket; without this guard the
  // timer fires on the main thread and races with ImageThread's recv(),
  // corrupting the message stream and hanging the GUI.
  connect(expTab, &ExposureTab::socketBusy, fwTab, &FilterWheelTab::setSocketBusy);

  // Disable ExposureTab's Start button while a wheel command is in flight so
  // the user cannot silently chain an image command onto the moving socket.
  connect(fwTab, &FilterWheelTab::wheelBusy, expTab, &ExposureTab::setWheelBusy);

  m_tabWidget->addTab(expTab,  tr("Exposures"));
  m_tabWidget->addTab(new AdvancedSettings(sock_pt), tr("Advanced Settings"));
  m_tabWidget->addTab(fwTab,   tr("Filter Wheel"));

  QVBoxLayout *mainLayout = new QVBoxLayout;
  mainLayout->addWidget(m_tabWidget);
  setLayout(mainLayout);

  setWindowTitle(tr("Andor2K Client"));

  printf("[DEBUG][ANDOR2K::client::%15s] TabDialog Socket at %p", __func__,
         &csock);
  if (csock)
    printf(" -> %p\n", csock);
  else
    printf(" -> nowhere!\n");
  printf("[DEBUG][ANDOR2K::client::%15s] Finished constructing TabDialog\n",
         __func__);
}
