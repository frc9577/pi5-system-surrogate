#include "imu_publisher.hpp"

#include "wpi/nt/DoubleArrayTopic.hpp"
#include "wpi/nt/DoubleTopic.hpp"

#include <array>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace surrogate {

struct ImuPublisher::Holder {
  std::vector<wpi::nt::DoubleArrayPublisher> array_pubs;
  std::vector<wpi::nt::DoublePublisher> scalar_pubs;
};

ImuPublisher::ImuPublisher(wpi::nt::NetworkTableInstance& inst)
    : holder_{std::make_unique<Holder>()} {
  // Topics that match HAL IMU.cpp:20-29. Sizes derived from the HAL's
  // expected subscriber types (3-vector accel/gyro, 4-vector quaternion,
  // 3-vector Euler in three orientations, scalar yaw in three orientations).
  static constexpr std::string_view kArrayTopics[] = {
      "/imu/rawaccel",         "/imu/rawgyro",         "/imu/quat",
      "/imu/euler_flat",       "/imu/euler_landscape", "/imu/euler_portrait",
  };
  static constexpr std::string_view kScalarTopics[] = {
      "/imu/yaw_flat",
      "/imu/yaw_landscape",
      "/imu/yaw_portrait",
  };
  static constexpr std::array<double, 3> kZero3 = {0.0, 0.0, 0.0};
  static constexpr std::array<double, 4> kQuatIdent = {1.0, 0.0, 0.0, 0.0};

  for (auto name : kArrayTopics) {
    auto pub = inst.GetDoubleArrayTopic(name).Publish();
    if (name == "/imu/quat") {
      pub.Set(std::span<const double>{kQuatIdent});
    } else {
      pub.Set(std::span<const double>{kZero3});
    }
    holder_->array_pubs.push_back(std::move(pub));
  }
  for (auto name : kScalarTopics) {
    auto pub = inst.GetDoubleTopic(name).Publish();
    pub.Set(0.0);
    holder_->scalar_pubs.push_back(std::move(pub));
  }
}

ImuPublisher::~ImuPublisher() = default;

}  // namespace surrogate
