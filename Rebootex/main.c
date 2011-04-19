#include <pspkernel.h>
#include <psputilsforkernel.h>
#include <pspsysevent.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>
#include "systemctrl.h"
#include "utils.h"
#include "../Rebootex_bin/rebootex.h"
#include "../PXE/Launcher/launcher_patch_offset.h"
#include "rebootex_conf.h"

u32 psp_model;
u32 psp_fw_version;

PSP_MODULE_INFO("fast_recovery", 0x1000, 1, 0);

extern u32 sceKernelGetModel_620(void);
extern u32 sceKernelDevkitVersion_620(void);
extern SceModule* sceKernelFindModuleByName_620(char *modname);

static int (*LoadReboot)(void * arg1, unsigned int arg2, void * arg3, unsigned int arg4) = NULL;

u32 sctrlKernelGetModel(void)
{
	u32 model = -1;

	switch(psp_fw_version) {
#ifdef CONFIG_635
		case FW_635:
			model = sceKernelGetModel();
			break;
#endif
#ifdef CONFIG_620
		case FW_620:
			model = sceKernelGetModel_620();
			break;
#endif
	};
   
	return model;
}

u32 sctrlKernelDevkitVersion(void)
{
	u32 fw_version;
   
	fw_version = sceKernelDevkitVersion_620();

	if(fw_version == 0x8002013A) {
		fw_version = sceKernelDevkitVersion();
	}

	return fw_version;
}

SceModule* sctrlKernelFindModuleByName(char *modname)
{
	SceModule *mod = NULL;

	switch(psp_fw_version) {
#ifdef CONFIG_635
		case FW_635:
			mod = sceKernelFindModuleByName(modname);
			break;
#endif
#ifdef CONFIG_620
		case FW_620:
			mod = sceKernelFindModuleByName_620(modname);
			break;
#endif
	};

	return mod;
}

void build_rebootex_configure(void)
{
	rebootex_config *conf = (rebootex_config *)(REBOOTEX_CONFIG);
	
	conf->magic = REBOOTEX_CONFIG_MAGIC;
	conf->psp_fw_version = psp_fw_version;
	conf->psp_model = psp_model;
}

//load reboot wrapper
int load_reboot(void * arg1, unsigned int arg2, void * arg3, unsigned int arg4)
{
	//copy reboot extender
	memcpy((void*)REBOOTEX_START, rebootex, size_rebootex);

	//reset reboot flags
	memset((void*)REBOOTEX_CONFIG, 0, 0x100);
	memset((void*)REBOOTEX_CONFIG_ISO_PATH, 0, 256);

	build_rebootex_configure();

	//forward
	return (*LoadReboot)(arg1, arg2, arg3, arg4);
}

// mode: 0 - OFW 1 - CFW
void patch_sceLoadExec(int mode)
{
	SceModule2 * loadexec = (SceModule2*)sctrlKernelFindModuleByName("sceLoadExec");
	u32 text_addr;
	struct sceLoadExecPatch *patch;

	if (loadexec == NULL) {
		return;
	}

	text_addr = loadexec->text_addr;

	if(psp_model == PSP_GO) { // PSP-N1000
		patch = &g_offs->loadexec_patch_05g;
	} else {
		patch = &g_offs->loadexec_patch_other;
	}
	
	//save LoadReboot function
	LoadReboot = (void*)loadexec->text_addr;

	if(mode == 0) {
		//restore LoadReboot
		_sw(MAKE_CALL(LoadReboot), loadexec->text_addr + patch->LoadRebootCall);

		//restore jmp to 0x88600000
		_sw(0x3C018860, loadexec->text_addr + patch->RebootJump);
	} else if(mode == 1) {
		//replace LoadReboot function
		_sw(MAKE_CALL(load_reboot), loadexec->text_addr + patch->LoadRebootCall);

		//patch Rebootex position to 0x88FC0000
		_sw(0x3C0188FC, loadexec->text_addr + patch->RebootJump); // lui $at, 0x88FC
	}

	sync_cache();
}

int main_thread(SceSize args, void *argp)
{
	int mode = 1;

	if(args == 4 && 0 == *(u32*)argp) {
		mode = 0;
	}

	patch_sceLoadExec(mode);
	sctrlKernelExitVSH(NULL);

	return 0;
}

int module_start(SceSize args, void* argp)
{
	int thid;

	psp_fw_version = sctrlKernelDevkitVersion();
	setup_patch_offset_table(psp_fw_version);
	psp_model = sctrlKernelGetModel();

	thid = sceKernelCreateThread("fastRecovery", main_thread, 0x1A, 0x1000, 0, NULL);

	if(thid>=0) {
		sceKernelStartThread(thid, args, argp);
	}

	return 0;
}

int module_stop(SceSize args, void *argp)
{
	return 0;
}
