#include <pspsdk.h> 
#include <pspkernel.h> 
#include <systemctrl.h> 
#include <psploadcore.h>
#include <string.h>
#include <stdio.h>


PSP_MODULE_INFO("btfree", 0x1000, 0, 0);


// Prototype not in the PSPSDK
int sceKernelQuerySystemCall(void* function);
int sceKernelQuerySystemCall63x(void* function); // Links to the real NID on 6.3x


// Just some error codes I found in the bt module
#define PSP_ERROR_BLUETOOTH_ALREADY_REGISTERED 0x802F0131
#define PSP_ERROR_BLUETOOTH_UNSUPPORTED_DEVICE 0x802F0135


#define MAKE_CALL(f) (0x0c000000 | (((u32)(f) >> 2)  & 0x03ffffff))
#define MAKE_SYSCALL(n) (0x03ffffff & (((u32)(n) << 6) | 0x0000000c))


// Previous start module handler
STMOD_HANDLER previousStartModuleHandler = NULL; 

// Address for the syscall stub in user memory
int blockAddress = 0;

// Firmware versin
int firmware = 0;

// This struct is passed to sub_09498 in bluetooth_plugin_module
typedef struct
{
	u32 stuct_size; // size of this struct = 0x54
	u16 item_number; // first item in array has 1, second has 2
	u16 name[32]; // in unicode
	u16 unknown1;
	u8 major_service_class; // probably this
	u8 major_device_class; // 1 = PC, 2 = phone, 4 = audio/video, 5 = peripheral device
	u8 minor_device_class; // different meaning depending on the major class
	u8 unknown2;
	u32 unknown3; // always the same for a given device
	u16 unknown4; // always the same for a given device
	u16 unknown5;
}
btDeviceInfo;


u32 patchAddresses_620[] = { 0x000095A4, 0x000094A8, 0x000094D4 };
u32 patchAddresses_63x[] = { 0x00013E00, 0x00013D04, 0x00013D30 };
u32* patchAddresses = NULL;


// Function pointer to the original sub_09498 in bluetooth_plugin_module
int (*bluetooth_plugin_module_sub_09498)(int, btDeviceInfo*, int) = NULL;



void fillBufferFromWidechar(unsigned short* inputBuffer, char* outputText)
{
  int i;
  for (i = 0; inputBuffer[i]; i++)
  {
    outputText[i] = inputBuffer[i];
  }

  outputText[i] = 0;
}


void logFilePrintf(char* format, int arg1)
{
	SceUID logfile = sceIoOpen("ef0:/btfree_log.txt", PSP_O_CREAT | PSP_O_WRONLY | PSP_O_APPEND, 0777);

	if (logfile > -1)
	{
		char buffer[100];
		sprintf(buffer, format, arg1);
		sceIoWrite(logfile, buffer, strlen(buffer));
		sceIoClose(logfile);
	}
}



int bluetooth_plugin_module_sub_09498_hook(int unknown, btDeviceInfo* devices, int count)
{
	int k1 = pspSdkSetK1(0);

	if (count > 0)
	{
		// Log the device info
		logFilePrintf("--------------------\n", 0);
		
		char name[32];
		int i;

		for (i = 0; i < count; i++)
		{
			fillBufferFromWidechar(devices[i].name, name);
			logFilePrintf("name         : %s\n", (u32)name);
			logFilePrintf("unknown1     : 0x%08lX\n", (u32)devices[i].unknown1);
			logFilePrintf("major_srv_cl : 0x%08lX\n", (u32)devices[i].major_service_class);
			logFilePrintf("major_dev_cl : 0x%08lX\n", (u32)devices[i].major_device_class);
			logFilePrintf("minor_dev_cl : 0x%08lX\n", (u32)devices[i].minor_device_class);
			logFilePrintf("unknown1     : 0x%08lX\n", (u32)devices[i].unknown2);
			logFilePrintf("unknown2     : 0x%08lX\n", (u32)devices[i].unknown3);
			logFilePrintf("unknown3     : 0x%08lX\n", (u32)devices[i].unknown4);
			logFilePrintf("unknown4     : 0x%08lX\n", (u32)devices[i].unknown5);
			logFilePrintf("\n", 0);

			// Device class can be changed here
			//devices[i].major_device_class = 2;
			//devices[i].minor_device_class = 4;
		}
	}

	pspSdkSetK1(k1);

	// Call the original function
	return bluetooth_plugin_module_sub_09498(unknown, devices, count);
}




int on_module_start(SceModule2* mod) 
{
	// Get active on the Bluetooth VSH plugin
	if (strcmp(mod->modname, "bluetooth_plugin_module") == 0) 
	{ 
		logFilePrintf("Entering on_module_start\n", 0);

		// Store function pointer to the original sub_09498
		bluetooth_plugin_module_sub_09498 = (void*)(mod->text_addr + 0x00009498);
		logFilePrintf("bluetooth_plugin_module_sub_09498 = 0x%08lX\n", (u32)bluetooth_plugin_module_sub_09498);

		// Setup a syscall stub in user memory
		if (blockAddress == 0)
		{
			SceUID blockId = sceKernelAllocPartitionMemory(2, "btfree_stub", PSP_SMEM_Low, 2 * sizeof(int), NULL);
			logFilePrintf("blockId = 0x%08lX\n", (u32)blockId);

			blockAddress = (int)sceKernelGetBlockHeadAddr(blockId);
			logFilePrintf("blockAddress = 0x%08lX\n", (u32)blockAddress);

			// Get syscall of the hook function
			int syscall;
			
			// Look if the regular function is resolved, if not use the 6.3x kernel NID
			if ((u32)&sceKernelQuerySystemCall != 0x0000054C) // syscall 0x15 = not linked
				syscall = sceKernelQuerySystemCall(&bluetooth_plugin_module_sub_09498_hook);
			else
				syscall = sceKernelQuerySystemCall63x(&bluetooth_plugin_module_sub_09498_hook);

			logFilePrintf("syscall = 0x%08lX\n", (u32)syscall);

			// Write syscall stub
			_sw(0x03E00008, blockAddress); // jr $ra
			_sw(MAKE_SYSCALL(syscall), blockAddress + sizeof(int)); // syscall
		}

		// Hook the call to the original function in bluetooth_plugin_module
		_sw(MAKE_CALL(blockAddress), mod->text_addr + patchAddresses[0]);


		// Now patch sub_09498 to accept any device class

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
		_sw(0x240EFFFF, mod->text_addr + patchAddresses[1]);

		// write "bne $v0, $t6, loc_000094E4", was "beq $v0, $t6, loc_000094E4"
		_sw(0x144E0003, mod->text_addr + patchAddresses[2]);
	} 

	// Call previously set start module handler if necessary
	if (previousStartModuleHandler)
		return previousStartModuleHandler(mod);
	else
		return 0;
} 



int module_start(SceSize args, void* argp)
{
	// Select patch address set for the firmware
	firmware = sceKernelDevkitVersion();

	logFilePrintf("Firmware version 0x%08lX\n", firmware);

	switch (firmware)
	{
		case 0x06020010:
			patchAddresses = patchAddresses_620;
			break;

		case 0x06030110:
		case 0x06030510:
			patchAddresses = patchAddresses_63x;
			break;
	}

	if (patchAddresses)
	{
		// Establish a handler that gets called before any modules "module_start" function is called.
		// A previous handler gets saved.
		previousStartModuleHandler = sctrlHENSetStartModuleHandler(on_module_start);
	}
	else
	{
		// Unsupported firmware
		logFilePrintf("This firmware is not supported.\n", 0);
	}

	return 0;
}





int module_stop(SceSize args, void* argp)
{
	// Restore the previous start module handler if there was one
	if (previousStartModuleHandler)
		sctrlHENSetStartModuleHandler(previousStartModuleHandler);

	return 0;
}
