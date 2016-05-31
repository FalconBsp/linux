#
# Makefile for industrial I/O accelerometer and gyroscope drivers
#

obj-$(CONFIG_IIO_BMI160_ACCEL_GYRO_6AXIS) += bmi160_accel_gyro.o
bmi160_accel_gyro-y := bmi160_accel_gyro_core.o \
		../accel/bmi160_accel_common.o \
		../gyro/bmi160_gyro_common.o

obj-$(CONFIG_IIO_BMI160_ACCEL_GYRO_I2C_6AXIS) += bmi160_accel_gyro_i2c.o
obj-$(CONFIG_IIO_BMI160_ACCEL_GYRO_SPI_6AXIS) += bmi160_accel_gyro_spi.o
