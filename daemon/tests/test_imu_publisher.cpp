#include "imu_publisher.hpp"

#include "nt_server.hpp"
#include "wpi/nt/DoubleArrayTopic.hpp"
#include "wpi/nt/DoubleTopic.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <vector>

using surrogate::ImuPublisher;
using namespace std::chrono_literals;

TEST(ImuPublisher, ConstructionPublishesZeroAccelAndGyro) {
  surrogate::NtServer server{56850};
  ImuPublisher imu{server.instance()};

  auto accel_sub = server.instance()
                       .GetDoubleArrayTopic("/imu/rawaccel")
                       .Subscribe(std::vector<double>{});
  auto gyro_sub = server.instance()
                      .GetDoubleArrayTopic("/imu/rawgyro")
                      .Subscribe(std::vector<double>{});
  std::this_thread::sleep_for(20ms);

  auto accel = accel_sub.Get();
  auto gyro = gyro_sub.Get();
  ASSERT_EQ(accel.size(), 3u);
  ASSERT_EQ(gyro.size(), 3u);
  for (auto v : accel) EXPECT_DOUBLE_EQ(v, 0.0);
  for (auto v : gyro) EXPECT_DOUBLE_EQ(v, 0.0);
}

TEST(ImuPublisher, QuatIsIdentity) {
  surrogate::NtServer server{56851};
  ImuPublisher imu{server.instance()};

  auto sub = server.instance()
                 .GetDoubleArrayTopic("/imu/quat")
                 .Subscribe(std::vector<double>{});
  std::this_thread::sleep_for(20ms);

  auto q = sub.Get();
  ASSERT_EQ(q.size(), 4u);
  EXPECT_DOUBLE_EQ(q[0], 1.0);
  EXPECT_DOUBLE_EQ(q[1], 0.0);
  EXPECT_DOUBLE_EQ(q[2], 0.0);
  EXPECT_DOUBLE_EQ(q[3], 0.0);
}

TEST(ImuPublisher, YawScalarsAreZero) {
  surrogate::NtServer server{56852};
  ImuPublisher imu{server.instance()};

  auto sub_flat =
      server.instance().GetDoubleTopic("/imu/yaw_flat").Subscribe(-1.0);
  auto sub_land =
      server.instance().GetDoubleTopic("/imu/yaw_landscape").Subscribe(-1.0);
  std::this_thread::sleep_for(20ms);

  EXPECT_DOUBLE_EQ(sub_flat.Get(), 0.0);
  EXPECT_DOUBLE_EQ(sub_land.Get(), 0.0);
}
