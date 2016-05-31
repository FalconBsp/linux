#
# Makefile for KMC debugger support code
#

ifeq ($(CONFIG_KMC_PATCH),y)
obj-y					+= kmc.o

ifeq ($(CONFIG_KMC_MODULE_DEBUG),y)
obj-y					+= kmc-mod.o
endif

ifeq ($(CONFIG_KMC_NO_USER_PATCH),y)
obj-y					+= kmc-support.o
endif

ifeq ($(CONFIG_KMC_EVENTTRACKER_SUPPORT),y)
obj-y					+= kmc-evtr.o
endif

endif

