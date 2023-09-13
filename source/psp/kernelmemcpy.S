	.set noreorder

#include "pspimport.s"

	IMPORT_START	"Kernel_Library",0x00010000
	IMPORT_FUNC	"Kernel_Library",0x1839852A,sceKernelMemcpy
	IMPORT_FUNC "Kernel_Library",0xA089ECA4,sceKernelMemset
