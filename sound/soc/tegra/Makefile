GCOV_PROFILE := y

subdir-ccflags-y := -Werror

# Tegra platform Support
snd-soc-tegra-pcm-objs := tegra_pcm.o
snd-soc-tegra-utils-objs += tegra_asoc_utils.o
snd-soc-tegra20-ac97-objs := tegra20_ac97.o
snd-soc-tegra20-das-objs := tegra20_das.o
snd-soc-tegra20-i2s-objs := tegra20_i2s.o
snd-soc-tegra20-spdif-objs := tegra20_spdif.o
snd-soc-tegra30-ahub-objs := tegra30_ahub.o
snd-soc-tegra30-i2s-objs := tegra30_i2s.o
snd-soc-tegra30-spdif-objs := tegra30_spdif.o
snd-soc-tegra30-dam-objs := tegra30_dam.o

obj-$(CONFIG_SND_SOC_TEGRA) += snd-soc-tegra-pcm.o
obj-$(CONFIG_SND_SOC_TEGRA) += snd-soc-tegra-utils.o
obj-$(CONFIG_SND_SOC_TEGRA20_AC97) += snd-soc-tegra20-ac97.o
obj-$(CONFIG_SND_SOC_TEGRA20_DAS) += snd-soc-tegra20-das.o
obj-$(CONFIG_SND_SOC_TEGRA20_I2S) += snd-soc-tegra20-i2s.o
obj-$(CONFIG_SND_SOC_TEGRA20_SPDIF) += snd-soc-tegra20-spdif.o
obj-$(CONFIG_SND_SOC_TEGRA30_AHUB) += snd-soc-tegra30-ahub.o
obj-$(CONFIG_SND_SOC_TEGRA30_DAM) += snd-soc-tegra30-dam.o
obj-$(CONFIG_SND_SOC_TEGRA30_I2S) += snd-soc-tegra30-i2s.o
obj-$(CONFIG_SND_SOC_TEGRA30_SPDIF) += snd-soc-tegra30-spdif.o

# Tegra machine Support
snd-soc-tegra-wm8753-objs := tegra_wm8753.o
snd-soc-tegra-wm8903-objs := tegra_wm8903.o
snd-soc-tegra-wm9712-objs := tegra_wm9712.o
snd-soc-tegra-trimslice-objs := trimslice.o
snd-soc-tegra-alc5632-objs := tegra_alc5632.o
snd-soc-tegra-rt5640-objs := tegra_rt5640.o
snd-soc-tegra-rt5645-objs := tegra_rt5645.o
snd-soc-tegra-rt5639-objs := tegra_rt5639.o

obj-$(CONFIG_SND_SOC_TEGRA_WM8753) += snd-soc-tegra-wm8753.o
obj-$(CONFIG_SND_SOC_TEGRA_WM8903) += snd-soc-tegra-wm8903.o
obj-$(CONFIG_SND_SOC_TEGRA_WM9712) += snd-soc-tegra-wm9712.o
obj-$(CONFIG_SND_SOC_TEGRA_TRIMSLICE) += snd-soc-tegra-trimslice.o
obj-$(CONFIG_SND_SOC_TEGRA_ALC5632) += snd-soc-tegra-alc5632.o
obj-$(CONFIG_SND_SOC_TEGRA_RT5640) += snd-soc-tegra-rt5640.o
obj-$(CONFIG_SND_SOC_TEGRA_RT5645) += snd-soc-tegra-rt5645.o
obj-$(CONFIG_SND_SOC_TEGRA_RT5639) += snd-soc-tegra-rt5639.o
