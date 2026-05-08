#include "linux_runtime.hpp"

#include <gtest/gtest.h>

#include <cstdlib>

using namespace surrogate::linux_runtime;
using surrogate::linux_runtime::NotifyKind;

TEST(LinuxRuntime, NotifyWithoutNotifySocketIsSilent) {
  ::unsetenv("NOTIFY_SOCKET");
  notify(NotifyKind::Ready);
  notify(NotifyKind::Watchdog);
  notify(NotifyKind::Stopping);
  notify_raw("CUSTOM=1");
  SUCCEED();
}

TEST(LinuxRuntime, NotifyWithBadSocketPathIsSilent) {
  ::setenv("NOTIFY_SOCKET", "/nonexistent/path/to/socket", 1);
  notify(NotifyKind::Ready);
  ::unsetenv("NOTIFY_SOCKET");
  SUCCEED();
}

TEST(LinuxRuntime, NotifyAbstractSocketPathDoesNotCrash) {
  // Abstract sockets begin with '@'. Even if no listener exists, sendto
  // returns ECONNREFUSED which we ignore (best-effort contract).
  ::setenv("NOTIFY_SOCKET", "@unused-abstract-socket-name", 1);
  notify(NotifyKind::Ready);
  ::unsetenv("NOTIFY_SOCKET");
  SUCCEED();
}

TEST(LinuxRuntime, MlockallReportsResultWithoutThrowing) {
  // We don't have CAP_IPC_LOCK as a normal user, so this likely returns
  // false. The contract is just "no throw".
  (void)lock_all_memory();
  SUCCEED();
}
