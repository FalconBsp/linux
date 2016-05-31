#
# Makefile for industrial I/O accelerometer drivers
#

obj-$(CONFIG_HID_SENSOR_ACCEL_3D) += hid-sensor-accel-3d.o

obj-$(CONFIG_IIO_ST_ACCEL_3AXIS) += st_accel.o
st_accel-y := st_accel_core.o
st_accel-$(CONFIG_IIO_BUFFER) += st_accel_buffer.o

obj-$(CONFIG_IIO_ST_ACCEL_I2C_3AXIS) += st_accel_i2c.o
obj-$(CONFIG_IIO_ST_ACCEL_SPI_3AXIS) += st_accel_spi.o

obj-$(CONFIG_IIO_BMI160_ACCEL_3AXIS) += bmi160_accel.o
bmi160_accel-y := bmi160_accel_core.o bmi160_accel_common.o
#bmi160_accel-$(CONFIG_IIO_BUFFER) += bmi160_accel_buffer.o

obj-$(CONFIG_IIO_BMI160_ACCEL_I2C_3AXIS) += bmi160_accel_i2c.o
#obj-$(CONFIG_IIO_BMI160_ACCEL_SPI_3AXIS) += bmi160_accel_spi.o

obj-$(CONFIG_KXSD9)	+= kxsd9.o
