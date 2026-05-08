#pragma once

#include "wpi/nt/NetworkTableInstance.hpp"

#include <memory>

namespace dssurrogate {

// Publishes /imu/* topics with zeros. Real SystemCore has an onboard IMU;
// our Pi doesn't, so we publish defaults so the HAL has something to read.
//
// Owns the publishers (kept alive for the daemon's lifetime).
class ImuPublisher {
 public:
  explicit ImuPublisher(wpi::nt::NetworkTableInstance& inst);
  ~ImuPublisher();  // out-of-line so Holder can be incomplete here

  ImuPublisher(const ImuPublisher&) = delete;
  ImuPublisher& operator=(const ImuPublisher&) = delete;

 private:
  // Publishers are kept alive but not exposed; they retain their last
  // (zeroed) value on the bus.
  struct Holder;
  std::unique_ptr<Holder> holder_;
};

}  // namespace dssurrogate
