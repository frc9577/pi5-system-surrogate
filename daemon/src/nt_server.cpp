#include "nt_server.hpp"

#include <print>

namespace surrogate {

NtServer::NtServer() : NtServer(kDefaultPort) {}

NtServer::NtServer(unsigned int port)
    : inst_{wpi::nt::NetworkTableInstance::Create()}, port_{port} {
  inst_.StartServer(/*persist_filename=*/"pi5-system-surrogate.json",
                    /*listen_address=*/"127.0.0.1",
                    /*mdns_service=*/"",
                    port_);
  std::println("nt_server: bound to 127.0.0.1:{}", port_);
}

NtServer::~NtServer() {
  inst_.StopServer();
  wpi::nt::NetworkTableInstance::Destroy(inst_);
}

}  // namespace surrogate
