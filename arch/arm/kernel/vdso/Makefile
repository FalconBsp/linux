obj-vdso := vgettimeofday.o

# Build rules
targets := $(obj-vdso) vdso.so vdso.so.dbg vdso.lds
obj-vdso := $(addprefix $(obj)/, $(obj-vdso))

ccflags-y := -shared -fPIC -fno-common -fno-builtin
ccflags-y += -nostdlib -Wl,-soname=linux-vdso.so.1 \
		$(call cc-ldoption, -Wl$(comma)--hash-style=sysv)

obj-y += vdso.o
extra-y += vdso.lds
CPPFLAGS_vdso.lds += -P -C -U$(ARCH)

CFLAGS_REMOVE_vdso.o = -pg
CFLAGS_REMOVE_vgettimeofday.o = -pg

# Disable gcov profiling for VDSO code
GCOV_PROFILE := n

# Force dependency
$(obj)/vdso.o : $(obj)/vdso.so

# Link rule for the .so file, .lds has to be first
SYSCFLAGS_vdso.so.dbg = $(c_flags)
$(obj)/vdso.so.dbg: $(src)/vdso.lds $(obj-vdso)
	$(call if_changed,vdsold)

# Strip rule for the .so file
$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

# Actual build commands
quiet_cmd_vdsold = VDSOL $@
      cmd_vdsold = $(CC) $(c_flags) -Wl,-T $^ -o $@

# Install commands for the unstripped file
quiet_cmd_vdso_install = INSTALL $@
      cmd_vdso_install = cp $(obj)/$@.dbg $(MODLIB)/vdso/$@

vdso.so: $(obj)/vdso.so.dbg
	@mkdir -p $(MODLIB)/vdso
	$(call cmd,vdso_install)

vdso_install: vdso.so
