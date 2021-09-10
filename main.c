#include <pspsdk.h> 
#include <pspkernel.h> 
#include <systemctrl.h> 
#include <psploadcore.h>
#include <string.h>


PSP_MODULE_INFO("btfree", 0x1000, 0, 0);

STMOD_HANDLER previousStartModuleHandler = NULL; 


int on_module_start(SceModule2* mod) 
{
	// Get active on the Bluetooth VSH plugin
	if (strcmp(mod->modname, "bluetooth_plugin_module") == 0) 
	{ 
		// There is a check for the device type that goes something like this:
		//
		// if (((descriptor & 0x000000FF) == 0x00000005) || (...) || (...)))
		//
		// It gets changed to:
		//
		// if (((descriptor & 0x000000FF) != 0x0000FFFF) || (...) || (...)))
		//
		// The result is obviously that the statement always evaluates as true,
		// therefore no devices are rejected early.

		// write "li $t6, 0xFFFF", was "li $t6, 0x5"
		*(u32*)(mod->text_addr + 0x000094A8) = 0x240EFFFF;

		// write "bne $v0, $t6, loc_000094E4", was "beq $v0, $t6, loc_000094E4"
		*(u32*)(mod->text_addr + 0x000094D4) = 0x144E0003;
	} 

	// Call previously set start module handler if necessary
	if (previousStartModuleHandler)
		return previousStartModuleHandler(mod);
	else
		return 0;
} 



int module_start(SceSize args, void* argp)
{
	// Establish a handler that gets called before any modules "module_start" function is called.
	// A previous handler gets saved.
	previousStartModuleHandler = sctrlHENSetStartModuleHandler(on_module_start);

	return 0;
}





int module_stop(SceSize args, void* argp)
{
	// Restore the previous start module handler if there was one
	if (previousStartModuleHandler)
		sctrlHENSetStartModuleHandler(previousStartModuleHandler);

	return 0;
}
