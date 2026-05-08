// Sample robot tester for pi5-system-surrogate.
//
// Connects to the daemon as a real WPILib NT4 client, validates the two
// contracts the SystemCore HAL would check at init: ServerReady=true and
// ControlData with DsConnected=true. Mirrors what HAL_Initialize() does
// on the wire, just without dragging in the full HAL.

plugins {
  java
  application
}

repositories {
  maven { url = uri("https://frcmaven.wpi.edu/artifactory/release/") }
  maven { url = uri("https://frcmaven.wpi.edu/artifactory/development/") }
  mavenCentral()
}

// 2027 alpha track. Bump as new alphas/betas land.
val wpilibVersion = "2027.0.0-alpha-5"

// Native classifier — we run this on the same aarch64 workstation that
// builds the daemon. For real Pi/SystemCore deploys, switch to
// "linuxsystemcore".
val nativeClassifier = "linuxarm64"

dependencies {
  implementation("edu.wpi.first.ntcore:ntcore-java:$wpilibVersion")
  implementation("edu.wpi.first.wpiutil:wpiutil-java:$wpilibVersion")
  runtimeOnly("edu.wpi.first.ntcore:ntcore-jni:$wpilibVersion:$nativeClassifier")
  runtimeOnly("edu.wpi.first.wpiutil:wpiutil-jni:$wpilibVersion:$nativeClassifier")
}

application {
  mainClass.set("DaemonTester")
}

java {
  toolchain {
    languageVersion.set(JavaLanguageVersion.of(17))
  }
}

// Build a fat jar with all dependencies bundled, so deploying the test is
// a single artifact: `java -jar build/libs/sample-robot-all.jar`.
tasks.register<Jar>("fatJar") {
  archiveClassifier.set("all")
  manifest { attributes["Main-Class"] = application.mainClass.get() }
  from(sourceSets.main.get().output)
  duplicatesStrategy = DuplicatesStrategy.EXCLUDE
  dependsOn(configurations.runtimeClasspath)
  from({
    configurations.runtimeClasspath.get().filter { it.name.endsWith("jar") }.map { zipTree(it) }
  })
}
