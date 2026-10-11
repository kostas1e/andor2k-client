#ifndef WHEELTHREAD_H
#define WHEELTHREAD_H

#include "helmos-andor2k/cpp_socket.hpp"

#include <QThread>
#include <cerrno>
#include <cstring>
#include <string>

// Sends one wheel command ("wheel home\n", "wheel setpos N\n", …) on a
// background thread. While the daemon streams intermediate QC updates
// ("fupper_qc:N;flower_qc:N;"), progressUpdate() is emitted for each one.
// When the daemon sends the final "done;error:N;" line, commandDone() is
// emitted. Mirrors the ImageThread streaming pattern for ExposureTab.
class WheelThread : public QThread {
  Q_OBJECT
public:
  explicit WheelThread(andor2k::ClientSocket *sock, const char *cmd, QObject *parent = nullptr)
      : QThread(parent), socket(sock), m_cmd(cmd) {
  }

private:
  andor2k::ClientSocket *socket;
  std::string m_cmd;

  void run() override {
    socket->send(m_cmd.c_str());
    char buf[256];
    bool done = false;
    while (!done) {
      std::memset(buf, 0, sizeof(buf));
      int n = socket->recv(buf, sizeof(buf) - 1);
      if (n < 0) {
        // EAGAIN / EWOULDBLOCK = SO_RCVTIMEO fired — daemon is still alive,
        // keep waiting. Any other error (ECONNRESET etc.) = daemon is gone.
        if (errno == EAGAIN || errno == EWOULDBLOCK)
          continue;
        break;
      }
      if (n == 0)
        break; // EOF: daemon closed connection
      if (std::strstr(buf, "done") != nullptr)
        done = true;
      else
        emit progressUpdate(QString(buf)); // intermediate QC line
    }
    emit commandDone(QString(buf));
  }

signals:
  void progressUpdate(const QString &);
  void commandDone(const QString &);
};

#endif // WHEELTHREAD_H
