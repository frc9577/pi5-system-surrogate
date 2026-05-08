#include "linux_runtime.hpp"

#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <print>

namespace dssurrogate::linux_runtime {

bool lock_all_memory() {
  if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
    std::println(stderr, "mlockall: {} (continuing without page-fault guard)",
                 std::strerror(errno));
    return false;
  }
  return true;
}

void notify(NotifyKind kind) {
  switch (kind) {
    case NotifyKind::Ready:    notify_raw("READY=1"); break;
    case NotifyKind::Watchdog: notify_raw("WATCHDOG=1"); break;
    case NotifyKind::Stopping: notify_raw("STOPPING=1"); break;
  }
}

void notify_raw(std::string_view payload) {
  const char* sock_path = std::getenv("NOTIFY_SOCKET");
  if (sock_path == nullptr || sock_path[0] == '\0') return;

  int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return;

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  // systemd accepts both filesystem paths and abstract sockets (path
  // beginning with '@'; on the wire the leading byte is NUL).
  if (sock_path[0] == '@') {
    addr.sun_path[0] = '\0';
    std::strncpy(addr.sun_path + 1, sock_path + 1, sizeof(addr.sun_path) - 2);
  } else {
    std::strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
  }

  ::sendto(fd, payload.data(), payload.size(), MSG_NOSIGNAL,
           reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  ::close(fd);
}

}  // namespace dssurrogate::linux_runtime
