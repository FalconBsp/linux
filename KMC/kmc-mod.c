/*
 * PARTNER-Jet Linux support patch by KMC
 *
 * 2012.06.10 ver3.0-beta
 */

#include <linux/spinlock.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kmc.h>

#include "__brk_code.h"

#ifdef noinline
#undef noinline
#define noinline        noinline
#endif
#define __KMC_DEBUG_FUNC_ATTR   __attribute__ ((noinline))
// #define      __KMC_DEBUG_FUNC_ATTR   __attribute__ ((aligned (4), noinline))

char __kmc_debug_module_name[8][64];
struct module *__kmc_debug_module[8];
void *__kmc_tmp_mod_array[7];

__KMC_DEBUG_FUNC_ATTR
void __kmc_mod_init_brfore(struct module *mod, int i)
{
	__KMC_BRK_CODE();
	asm("   .long   0x4c434d88");
}

__KMC_DEBUG_FUNC_ATTR
void __kmc_mod_init_after(struct module *mod)
{
	__KMC_BRK_CODE();
	asm("   .long   0x4c434d89");
}

__KMC_DEBUG_FUNC_ATTR
void __kmc_mod_exit(struct module *mod)
{
	__KMC_BRK_CODE();
	asm("   .long   0x4c434d8a");
}

void __kmc_mod_init_brfore__(struct module *mod)
{

	int	i, st;

	for (i = 0; i < 8; i++) {
		st = strnicmp(mod->name, __kmc_debug_module_name[i], 64);
		if (0 == st) {
			__kmc_debug_module[i] = mod;
			__kmc_mod_init_brfore(mod, i);
			break;
		} else {
			if (strchr(__kmc_debug_module_name[i], '-')) {
				char	__tmp_name[64], *p;
				p = strcpy(__tmp_name, __kmc_debug_module_name[i]);
				while ((p = strchr(p, '-')) != NULL) {
					*p = '_';
				}
				st = strnicmp(mod->name, __tmp_name, 64);
				if (0 == st) {
					__kmc_debug_module[i] = mod;
					__kmc_mod_init_brfore(mod, i);
					break;
				}
			}
		}
	}

	return;
}

void __kmc_mod_init_after__(struct module *mod, int ret)
{
	int	i;

	for (i = 0; i < 8; i++) {
		if (__kmc_debug_module[i] == mod) {
			__kmc_mod_init_after(mod);
			if (0 != ret) {
				__kmc_mod_exit(mod);
				__kmc_debug_module[i] = (void *)0;
			}
			break;
		}
	}

	return;
}

void __kmc_mod_exit__(struct module *mod)
{
	int	i;

	for (i = 0; i < 8; i++) {
		if (__kmc_debug_module[i] == mod) {
			__kmc_mod_exit(mod);
			__kmc_debug_module[i] = (void *)0;
			break;
		}
	}

	return;
}

void __kmc_chk_mod_sec__(struct module *mod, char *secname, void *adr)
{
	if (0 == strnicmp(".text", secname, sizeof(".text"))) {
		__kmc_tmp_mod_array[0] = adr;
		goto out;
	}
	if (0 == strnicmp(".data", secname, sizeof(".data"))) {
		__kmc_tmp_mod_array[1] = adr;
		goto out;
	}
	if (0 == strnicmp(".bss", secname, sizeof(".bss"))) {
		__kmc_tmp_mod_array[2] = adr;
		goto out;
	}
	if (0 == strnicmp(".exit.text", secname, sizeof(".exit.text"))) {
		__kmc_tmp_mod_array[3] = adr;
		goto out;
	}
	if (0 == strnicmp(".exit.data", secname, sizeof(".exit.data"))) {
		__kmc_tmp_mod_array[4] = adr;
		goto out;
	}
	if (0 == strnicmp(".init.text", secname, sizeof(".init.text"))) {
		__kmc_tmp_mod_array[5] = adr;
		goto out;
	}
	if (0 == strnicmp(".init.data", secname, sizeof(".init.data"))) {
		__kmc_tmp_mod_array[6] = adr;
	}
out:
	return;
}

void __kmc_set_mod_sec(struct module *mod)
{
	mod->__kmc_mod_text = __kmc_tmp_mod_array[0];
	mod->__kmc_mod_data = __kmc_tmp_mod_array[1];
	mod->__kmc_mod_bss = __kmc_tmp_mod_array[2];
	mod->__kmc_mod_exit_text = __kmc_tmp_mod_array[3];
	mod->__kmc_mod_exit_data = __kmc_tmp_mod_array[4];
	mod->__kmc_mod_init_text = __kmc_tmp_mod_array[5];
	mod->__kmc_mod_init_data = __kmc_tmp_mod_array[6];
	memset(__kmc_tmp_mod_array, 0, sizeof(__kmc_tmp_mod_array));

	return;
}





