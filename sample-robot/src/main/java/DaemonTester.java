// DaemonTester — minimal NT4 client that validates ds-surrogate's HAL
// contract. Replicates the two checks HAL_Initialize() would do:
//
//   1. /Netcomm/Control/ServerReady = true (else HAL terminates after 10s)
//   2. /Netcomm/Control/ControlData (mrc.proto.ProtobufControlData) bytes
//      with the DsConnected bit set (else HAL_RefreshDSData zeros the
//      control word and the robot is permanently disabled)
//
// Exits 0 on full pass, 1 on any timeout or contract violation. Run with:
//
//   ./gradlew run                 # against daemon on localhost:6810
//   ./gradlew run --args "host"   # custom host
//
// Or build the fat jar and invoke directly:
//
//   ./gradlew fatJar
//   java -jar build/libs/sample-robot-all.jar [host]

import org.wpilib.networktables.BooleanSubscriber;
import org.wpilib.networktables.NetworkTableInstance;
import org.wpilib.networktables.PubSubOption;
import org.wpilib.networktables.RawSubscriber;

public final class DaemonTester {

  private static final int kPort = 6810;
  private static final int kTimeoutMs = 4000;
  private static final int kPollMs = 50;

  // Bit layout of mrc::ControlData::ControlWord (mirrors NetComm.h).
  private static final int kCwEnabledBit = 1 << 0;
  private static final int kCwDsConnectedBit = 1 << 5;

  public static void main(String[] args) throws Exception {
    String host = args.length > 0 ? args[0] : "127.0.0.1";

    NetworkTableInstance inst = NetworkTableInstance.create();
    try {
      inst.setServer(host, kPort);
      inst.startClient4("sample-robot");

      boolean ok = waitForServerReady(inst) && validateControlData(inst);
      System.exit(ok ? 0 : 1);
    } finally {
      inst.stopClient();
      NetworkTableInstance.destroy(inst);
    }
  }

  private static boolean waitForServerReady(NetworkTableInstance inst)
      throws InterruptedException {
    BooleanSubscriber sub =
        inst.getBooleanTopic("/Netcomm/Control/ServerReady").subscribe(false);
    for (int waited = 0; waited <= kTimeoutMs; waited += kPollMs) {
      if (sub.get()) {
        System.out.printf("[PASS] ServerReady=true (after %d ms)%n", waited);
        return true;
      }
      Thread.sleep(kPollMs);
    }
    System.err.printf(
        "[FAIL] ServerReady not received within %d ms%n", kTimeoutMs);
    return false;
  }

  private static boolean validateControlData(NetworkTableInstance inst)
      throws InterruptedException {
    RawSubscriber sub =
        inst.getRawTopic("/Netcomm/Control/ControlData")
            .subscribe(
                "proto:mrc.proto.ProtobufControlData",
                new byte[0],
                PubSubOption.sendAll(true));

    for (int waited = 0; waited <= kTimeoutMs; waited += kPollMs) {
      byte[] bytes = sub.get();
      if (bytes != null && bytes.length > 0) {
        int controlWord = decodeControlWord(bytes);
        boolean dsConnected = (controlWord & kCwDsConnectedBit) != 0;
        boolean enabled = (controlWord & kCwEnabledBit) != 0;
        System.out.printf(
            "[%s] ControlData received (%d bytes, ControlWord=0x%x, "
                + "DsConnected=%s, Enabled=%s) after %d ms%n",
            dsConnected ? "PASS" : "FAIL",
            bytes.length, controlWord, dsConnected, enabled, waited);
        if (!dsConnected) {
          System.err.println(
              "[FAIL] DsConnected bit not set; HAL_RefreshDSData would zero"
                  + " the control word.");
          return false;
        }
        return true;
      }
      Thread.sleep(kPollMs);
    }
    System.err.printf(
        "[FAIL] ControlData not received within %d ms%n", kTimeoutMs);
    return false;
  }

  /**
   * Tiny protobuf varint reader to extract just ControlWord (field tag 5).
   * We don't need a full protobuf parser — only this one field. Tag 5
   * varint is wire byte 0x28 (5 << 3 | 0); value follows as a varint.
   */
  private static int decodeControlWord(byte[] bytes) {
    int i = 0;
    while (i < bytes.length) {
      int tag = bytes[i++] & 0xff;
      int fieldNum = tag >>> 3;
      int wireType = tag & 0x7;
      if (fieldNum == 5 && wireType == 0) {
        // varint payload
        int result = 0;
        int shift = 0;
        while (i < bytes.length) {
          int b = bytes[i++] & 0xff;
          result |= (b & 0x7f) << shift;
          if ((b & 0x80) == 0) break;
          shift += 7;
        }
        return result;
      }
      // Skip other fields based on wire type.
      if (wireType == 0) {
        while (i < bytes.length && (bytes[i++] & 0x80) != 0) {}
      } else if (wireType == 2) {
        int len = 0;
        int shift = 0;
        while (i < bytes.length) {
          int b = bytes[i++] & 0xff;
          len |= (b & 0x7f) << shift;
          if ((b & 0x80) == 0) break;
          shift += 7;
        }
        i += len;
      } else if (wireType == 1) {
        i += 8;
      } else if (wireType == 5) {
        i += 4;
      } else {
        break;  // unsupported wire type
      }
    }
    return 0;
  }
}
