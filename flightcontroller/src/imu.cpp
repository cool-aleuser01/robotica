#include <stdexcept>
#include <cmath>
#include <unistd.h>
#include <Eigen/Dense>

#include "i2c.hpp"
#include "imu.hpp"
#include "kalman.hpp"

const double PI = 3.1415926535897;
const double RAD_TO_DEG = 180.0 / PI;

void delay(long msecs) {
    usleep(msecs * 1000);
}

long micros() {
    static timespec t_start;
    static bool init = false;

    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);

    if (!init) {
        t_start = t;
        init = true;
    }

    return (t.tv_sec - t_start.tv_sec) * 1000 * 1000 + t.tv_nsec / 1000;
}

long millis() {
    return micros() / 1000;
}

imu& imu::get() {
    static imu instance;
    return instance;
}

imu::imu() : calibrated(false), start_t(0), yawOffset(0) {
    i2cData[0] = 7; // Set sample rate to 1000 Hz = 8000 Hz / (7 + 1)
    i2cData[1] = 0x00; // Disable FSYNC and set 260 Hz Acc filtering, 256 Hz Gyro filtering, 8 KHz sampling
    i2cData[2] = 0x00; // Set Gyro Full Scale Range to ±250deg/s
    i2cData[3] = 0x00; // Set Accelerometer Full Scale Range to ±2g

    while (i2c::write(0x19, i2cData, 4)); // Write to all four registers at once
    while (i2c::write(0x6B, 0x01)); // PLL with X axis gyroscope reference and disable sleep mode

    while (i2c::read(0x75, i2cData, 1));
    if (i2cData[0] != 0x68) {
        throw std::runtime_error("failed to detect IMU");
    }

    // Let sensor stabilise
    delay(100);

    // Set kalman and gyro start angle
    while (i2c::read(0x3B, i2cData, 6));
    accX = (i2cData[0] << 8) | i2cData[1];
    accY = (i2cData[2] << 8) | i2cData[3];
    accZ = (i2cData[4] << 8) | i2cData[5];

    // Calculate roll/pitch using accelerometer, restricting roll to +-90 deg
    double roll  = atan(accY / sqrt(accX * accX + accZ * accZ)) * RAD_TO_DEG;
    double pitch = atan2(-accX, accZ) * RAD_TO_DEG;

    kalmanX.setAngle(roll);
    kalmanY.setAngle(pitch);

    gyroXangle = roll;
    gyroYangle = pitch;
    compAngleX = roll;
    compAngleY = pitch;

    timer = micros();
    start_t = millis();
}

void imu::poll() {
    // Update values
    while (i2c::read(0x3B, i2cData, 14));
    accX = ((i2cData[0] << 8) | i2cData[1]);
    accY = ((i2cData[2] << 8) | i2cData[3]);
    accZ = ((i2cData[4] << 8) | i2cData[5]);
    tempRaw = (i2cData[6] << 8) | i2cData[7];
    gyroX = (i2cData[8] << 8) | i2cData[9];
    gyroY = (i2cData[10] << 8) | i2cData[11];
    gyroZ = (i2cData[12] << 8) | i2cData[13];

    double dt = (double)(micros() - timer) / 1000000;
    timer = micros();

    double roll  = atan(accY / sqrt(accX * accX + accZ * accZ)) * RAD_TO_DEG;
    double pitch = atan2(-accX, accZ) * RAD_TO_DEG;

    double gyroXrate = gyroX / 131.0;
    double gyroYrate = gyroY / 131.0;
    double gyroZrate = gyroZ / 131.0;

    // This fixes the transition problem when the accelerometer angle jumps between -180 and 180 degrees
    if ((pitch < -90 && kalAngleY > 90) || (pitch > 90 && kalAngleY < -90)) {
        kalmanY.setAngle(pitch);
        compAngleY = pitch;
        kalAngleY = pitch;
        gyroYangle = pitch;
    } else
        kalAngleY = kalmanY.getAngle(pitch, gyroYrate, dt); // Calculate the angle using a Kalman filter

    if (abs(kalAngleY) > 90)
        gyroXrate = -gyroXrate; // Invert rate, so it fits the restriced accelerometer reading

    kalAngleX = kalmanX.getAngle(roll, gyroXrate, dt); // Calculate the angle using a Kalman filter

    gyroXangle += gyroXrate * dt; // Calculate gyro angle without any filter
    gyroYangle += gyroYrate * dt;
    gyroZangle += gyroZrate * dt - yawOffset * dt;

    speed[1] += accX * dt;
    speed[2] += accY * dt;
    speed[3] += (accZ - 9.81) * dt;

    // Determine yaw offset
    if (!calibrated && millis() - start_t >= 1000) {
        yawOffset = gyroZangle;
        calibrated = true;
        gyroZangle = 0;
    }

    compAngleX = 0.93 * (compAngleX + gyroXrate * dt) + 0.07 * roll; // Calculate the angle using a Complimentary filter
    compAngleY = 0.93 * (compAngleY + gyroYrate * dt) + 0.07 * pitch;

    if (gyroXangle < -180 || gyroXangle > 180)
        gyroXangle = kalAngleX;
    if (gyroYangle < -180 || gyroYangle > 180)
        gyroYangle = kalAngleY;
}

Eigen::Vector3f imu::get_angles() const {
    return Eigen::Vector3f(kalAngleX, kalAngleY, gyroZangle);
}

Eigen::Vector3f imu::get_acceleration() const {
    return Eigen::Vector3f(accX, accY, accZ);
}

Eigen::Vector3f imu::get_speed() const {
    return speed;
}
float imu::get_temperature() const {
    return (float) tempRaw / 340.0f + 36.53f;
}