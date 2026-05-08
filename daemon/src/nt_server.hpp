#pragma once

#include "wpi/nt/NetworkTableInstance.hpp"

namespace surrogate {

// Owns a wpi::nt::NetworkTableInstance bound as an NT4 server on
// localhost:6810 — the contract the WPILib SystemCore HAL connects to as
// a client. The HAL polls /Netcomm/Control/ServerReady at init and calls
// std::terminate() if it doesn't see true within ~10 seconds.
class NtServer {
 public:
  static constexpr unsigned int kDefaultPort = 6810;

  // Default constructs bound to localhost:6810 — the contract the WPILib
  // SystemCore HAL connects to. Tests use the port-explicit overload to
  // avoid clashing with a running daemon on 6810.
  NtServer();
  explicit NtServer(unsigned int port);
  ~NtServer();

  NtServer(const NtServer&) = delete;
  NtServer& operator=(const NtServer&) = delete;

  wpi::nt::NetworkTableInstance& instance() noexcept { return inst_; }
  unsigned int port() const noexcept { return port_; }

 private:
  wpi::nt::NetworkTableInstance inst_;
  unsigned int port_;
};

}  // namespace surrogate
