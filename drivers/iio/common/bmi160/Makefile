#
# Makefile for the BMI160 common modules.
#

obj-$(CONFIG_IIO_BMI160_I2C) += bmi160_i2c.o
obj-$(CONFIG_IIO_BMI160_SPI) += bmi160_spi.o
obj-$(CONFIG_IIO_BMI160_CORE) += bmi160.o
bmi160-y := bmi160_core.o
bmi160-$(CONFIG_IIO_BUFFER) += bmi160_buffer.o
#bmi160-$(CONFIG_IIO_TRIGGER) += bmi160_trigger.o
