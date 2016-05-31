GCOV_PROFILE := y

asflags-y				+= -march=armv7-a
#subdir-ccflags-y := -Werror

obj-y                                   += ahb.o
obj-y                                   += common.o
obj-y                                   += devices.o
obj-y                                   += board-info.o
obj-y                                   += io.o
obj-y                                   += clock.o
ifneq ($(CONFIG_CLK_SRC_TEGRA_TIMER),y)
obj-y                                   += timer.o
endif
obj-$(CONFIG_HAVE_ARM_TWD)              += cpu_timer_twd.o
obj-y                                   += timerinfo.o
ifneq ($(CONFIG_ARCH_TEGRA_2x_SOC),y)
ifneq ($(CONFIG_ARCH_TEGRA_3x_SOC),y)
obj-y                                   += mipi-cal.o
endif
endif

obj-$(CONFIG_PM_SLEEP)                  += tegra-wakeups.o

ifeq ($(CONFIG_ARCH_TEGRA_2x_SOC),y)
ifneq ($(CONFIG_CLK_SRC_TEGRA_TIMER),y)
obj-y                                   += timer-t2.o
endif
ifeq ($(CONFIG_CPU_IDLE),y)
obj-$(CONFIG_PM_SLEEP)                  += cpuidle-t2.o
endif
else
obj-y                                   += pm-t3.o
obj-y                                   += sleep-t30.o
obj-$(CONFIG_DEBUG_FS)                  += clocks_stats.o
ifneq ($(CONFIG_CLK_SRC_TEGRA_TIMER),y)
obj-y                                   += timer-t3.o
endif
obj-y                                   += tegra_core_volt_cap.o
obj-$(CONFIG_TEGRA_USE_SIMON)           += tegra_simon.o
endif
ifeq ($(CONFIG_ARCH_TEGRA_12x_SOC),y)
obj-$(CONFIG_PM_SLEEP)			+= wakeups-t12x.o
endif

ifeq ($(CONFIG_PM_SLEEP),y)
obj-$(CONFIG_ARCH_TEGRA_12x_SOC)        += mc-timing-t12x.o
endif

ifeq ($(CONFIG_CPU_IDLE),y)
ifeq ($(CONFIG_PM_SLEEP),y)
obj-$(CONFIG_ARCH_TEGRA_12x_SOC)        += cpuidle-t11x.o
endif
endif

obj-y                                   += delay.o
obj-y                                   += pm.o
obj-$(CONFIG_TEGRA_WDT_RECOVERY)        += wdt-recovery.o
obj-$(CONFIG_PM_SLEEP)                  += pm-irq.o

obj-y                                   += sleep.o
obj-$(CONFIG_TEGRA_USE_NCT)             += nct.o nct_sysfs.o

plus_sec := $(call as-instr,.arch_extension sec,+sec)
AFLAGS_sleep.o :=-Wa,-march=armv7-a$(plus_sec)

obj-y                                   += kfuse.o
obj-$(CONFIG_TEGRA_PTM)                 += tegra_ptm.o

obj-y                                   += powergate.o
obj-y                                   += powergate-ops-txx.o
obj-y                                   += powergate-ops-t1xx.o
obj-$(CONFIG_ARCH_TEGRA_12x_SOC)        += powergate-t12x.o

obj-y                                   += apbio.o
obj-y                                   += mc.o
obj-$(CONFIG_PM_GENERIC_DOMAINS)        += pm_domains.o
obj-y                                   += dvfs.o
obj-$(CONFIG_ARCH_TEGRA_12x_SOC)        += tegra12_dvfs.o

obj-y                                   += latency_allowance.o
obj-$(CONFIG_ARCH_TEGRA_12x_SOC)        += tegra12x_la.o
obj-$(CONFIG_TEGRA_EDP_LIMITS)          += edp.o
obj-$(CONFIG_TEGRA_CORE_EDP_LIMITS)     += edp_core.o
obj-$(CONFIG_ARCH_TEGRA_12x_SOC)        += tegra12_edp.o
obj-$(CONFIG_ARCH_TEGRA_12x_SOC)        += tegra12_speedo.o
obj-$(CONFIG_ARCH_TEGRA_12x_SOC)        += tegra3_actmon.o
obj-$(CONFIG_ARCH_TEGRA_12x_SOC)        += tegra12_emc.o
obj-$(CONFIG_ARCH_TEGRA_12x_SOC)        += tegra_emc_dt_parse.o
obj-y					+= tegra_emc.o
obj-$(CONFIG_HOTPLUG_CPU)               += hotplug.o
obj-y                                   += pmc.o
obj-y                                   += flowctrl.o
obj-$(CONFIG_CPU_IDLE)                  += cpuidle.o
obj-$(CONFIG_CPU_IDLE)                  += sleep.o
obj-$(CONFIG_ARCH_TEGRA_12x_SOC)        += tegra12_clocks.o
obj-$(CONFIG_SMP)                       += platsmp.o
obj-y                                   += reset.o
obj-y                                   += headsmp.o
obj-$(CONFIG_CPU_FREQ)                  += cpu-tegra.o
ifeq ($(CONFIG_TEGRA_AUTO_HOTPLUG),y)
ifeq ($(CONFIG_TEGRA_CPUQUIET),y)
obj-$(CONFIG_ARCH_TEGRA_12x_SOC)        += cpuquiet.o
endif
endif
ifeq ($(CONFIG_TEGRA_SOCTHERM),y)
obj-$(CONFIG_ARCH_TEGRA_12x_SOC)        += tegra11_soctherm.o
endif
ifeq ($(CONFIG_TEGRA_THERMAL_THROTTLE),y)
obj-$(CONFIG_ARCH_TEGRA_12x_SOC)        += tegra3_throttle.o
endif
obj-$(CONFIG_ARCH_TEGRA_HAS_CL_DVFS)    += tegra_cl_dvfs.o

obj-y					+= board-common.o

# MC error reporting.
ifeq ($(CONFIG_ARCH_TEGRA_2x_SOC),)
obj-y                                   += mcerr.o
obj-$(CONFIG_ARCH_TEGRA_12x_SOC)        += mcerr-t12.o
endif

obj-y					+= board-panel.o
obj-y					+= panel-p-wuxga-10-1.o
obj-y					+= panel-lgd-wxga-7-0.o

obj-${CONFIG_MACH_ARDBEG}               += board-ardbeg.o
obj-${CONFIG_MACH_ARDBEG}               += board-ardbeg-sdhci.o
obj-${CONFIG_MACH_ARDBEG}               += board-ardbeg-memory.o
obj-${CONFIG_MACH_ARDBEG}               += board-ardbeg-powermon.o
obj-${CONFIG_MACH_ARDBEG}		+= board-ardbeg-panel.o
obj-${CONFIG_MACH_LAGUNA}		+= board-laguna-power.o

obj-${CONFIG_MACH_ARDBEG}		+= panel-a-edp-1080p-14-0.o
obj-${CONFIG_MACH_ARDBEG}		+= panel-c-lvds-1366-14.o
obj-${CONFIG_MACH_ARDBEG}		+= panel-a-1200-800-8-0.o
obj-${CONFIG_MACH_ARDBEG}		+= panel-a-1200-1920-7-0.o
obj-${CONFIG_MACH_LAGUNA}		+= panel-a-1080p-14-0.o
obj-${CONFIG_MACH_ARDBEG}		+= panel-s-wqxga-10-1.o

obj-${CONFIG_TEGRA_ISOMGR}              += isomgr.o
