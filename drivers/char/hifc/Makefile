#
# Makefile for CXD5602	HostIFC
#

hifc_prot-y := hifc_prot_stack.o hifc_prot_task_mgr.o
obj-$(CONFIG_HIFC_MSG_DISP) += hifc_prot.o
obj-$(CONFIG_HIFC_MSG_DISP) += hifc_msg_disp.o

ifdef CONFIG_HIFC_DRVIF_SPI
	obj-$(CONFIG_HIFC_MSG_DISP) += hifc_drvif_spi.o
else
	obj-$(CONFIG_HIFC_MSG_DISP) += hifc_drvif_i2c.o
endif
