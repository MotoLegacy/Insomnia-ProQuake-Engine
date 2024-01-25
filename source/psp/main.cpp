/*
Copyright (C) 2007 Peter Mackay and Chris Swindle.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#define CONSOLE_DEBUG 0

#include <stdexcept>
#include <vector>
#include <sys/unistd.h>

#include <pspdisplay.h>
#include <pspdebug.h>
#include <pspkernel.h>
#include <pspmoduleinfo.h>
#include <psppower.h>
#include <psprtc.h>
#include <pspctrl.h>
#include <pspsdk.h>

extern "C"
{
#include "../quakedef.h"
#include "sysmem_module.h"
#include "m33libs/include/kubridge.h"
int pspDveMgrSetVideoOut(int, int, int, int, int, int, int);
void VramSetSize(int kb);
}

#include "battery.hpp"
#include "system.hpp"

// Running a dedicated server?
qboolean isDedicated = qfalse;
qboolean exec_id1_config;

extern	int  com_argc;
extern	char **com_argv;

int psp_system_model;

void Sys_ReadCommandLineFile (char* netpath);

#define printf	pspDebugScreenPrintf
#define MIN_HEAP_MB	6
#define MAX_HEAP_MB 40

namespace quake
{
	namespace main
	{
	// Clock speeds.
		extern const int		cpuClockSpeed	= scePowerGetCpuClockFrequencyInt();
		extern const int		ramClockSpeed	= cpuClockSpeed;
		extern const int		busClockSpeed	= scePowerGetBusClockFrequencyInt();

		// Static heap size
		static size_t heapSize;

		// Should the main loop stop running?
		static volatile bool	quit			= false;

		// Is the PSP in suspend mode?
		static volatile bool	suspended		= false;

		static int exitCallback(int arg1, int arg2, void* common)
		{
			// Signal the main thread to stop.
			quit = true;
			return 0;
		}

		static int powerCallback(int unknown, int powerInfo, void* common)
		{
			if (powerInfo & PSP_POWER_CB_POWER_SWITCH || powerInfo & PSP_POWER_CB_SUSPENDING)
			{
				suspended = true;
			}
			else if (powerInfo & PSP_POWER_CB_RESUMING)
			{
			}
			else if (powerInfo & PSP_POWER_CB_RESUME_COMPLETE)
			{
				suspended = false;
			}

			return 0;
		}

		static int callback_thread(SceSize args, void *argp)
		{
			// Register the exit callback.
			const SceUID exitCallbackID = sceKernelCreateCallback("exitCallback", exitCallback, NULL);
			sceKernelRegisterExitCallback(exitCallbackID);

			// Register the power callback.
			const SceUID powerCallbackID = sceKernelCreateCallback("powerCallback", powerCallback, NULL);
			scePowerRegisterCallback(0, powerCallbackID);

			// Sleep and handle callbacks.
			sceKernelSleepThreadCB();
			return 0;
		}

		static int setUpCallbackThread(void)
		{
			const int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, PSP_THREAD_ATTR_USER, 0);
			if (thid >= 0)
				sceKernelStartThread(thid, 0, 0);
			return thid;
		}


		static void disableFloatingPointExceptions()
		{
#ifndef _WIN32
			asm(
				".set push\n"
				".set noreorder\n"
				"cfc1    $2, $31\n"		// Get the FP Status Register.
				"lui     $8, 0x80\n"	// (?)
				"and     $8, $2, $8\n"	// Mask off all bits except for 23 of FCR. (? - where does the 0x80 come in?)
				"ctc1    $8, $31\n"		// Set the FP Status Register.
				".set pop\n"
				:						// No inputs.
				:						// No outputs.
				: "$2", "$8"			// Clobbered registers.
				);
#endif
		}
	}
}

extern bool bmg_type_changed;

using namespace quake;
using namespace quake::main;

#define MAX_NUM_ARGVS	50
#define MAX_ARG_LENGTH  64
static char *empty_string = "";
char    *f_argv[MAX_NUM_ARGVS];
int     f_argc;
int 	user_main(SceSize argc, void* argp);

int CheckParm (char **args, int argc, char *parm)
{
	int  i;
	
	for (i=1 ; i<argc ; i++)
	{
		if (!args[i])
			continue;               // NEXTSTEP sometimes clears appkit vars.
		if (!strcmp (parm,args[i]))
			return i;
	}
		
	return 0;
}

int Sys_GetPSPModel(void) 
{
	// pspemu has this module on its flash0 partition
	int vitaprx = sceIoOpen("flash0:/kd/registry.prx", PSP_O_RDONLY | PSP_O_WRONLY, 0777);

	if (vitaprx >= 0) {
		sceIoClose(vitaprx);
		return PSP_MODEL_PSVITA;
	}

	int model = kuKernelGetModel();

	if (model == 0)
		return PSP_MODEL_PHAT;

	return PSP_MODEL_SLIM;
}

typedef struct {
	char* pretty_name;
	char* dir;
	int heap_phat;
	int heap_slim;
	int status_phat;
	int status_slim;
	int compat_mode;
	bool has_pretty_name;
	bool occupied;
	bool exec_id1_config;
} uimod_t;

#define GAME_STATUS_UNKNOWN 	0
#define GAME_STATUS_PERFECT 	1
#define GAME_STATUS_DECENT 		2
#define GAME_STATUS_BAD 		3

uimod_t games_found[25];

//
// COMPATIBILITY LIST
//
void register_game(const char* dirname, int index)
{
	games_found[index].has_pretty_name = false;
	games_found[index].heap_phat = games_found[index].heap_slim = (int)heapSize;
	games_found[index].occupied = true;
	games_found[index].dir = static_cast<char*>(malloc(sizeof(char)*32));
	sprintf(games_found[index].dir, "%s", dirname);
	games_found[index].status_phat = games_found[index].status_slim = GAME_STATUS_UNKNOWN;
	games_found[index].exec_id1_config = false;
	games_found[index].compat_mode = COMPAT_QUAKE;

	// Quake
	if (strcasecmp(dirname, "id1") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*8));
		sprintf(games_found[index].pretty_name, "Quake", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 20 * 1024 * 1024;
		games_found[index].status_phat = games_found[index].status_slim = GAME_STATUS_PERFECT;
	} 
	// Quake Mission Pack 1: Scourge of Armagon
	else if (strcasecmp(dirname, "hipnotic") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*64));
		sprintf(games_found[index].pretty_name, "Quake Mission Pack 1: Scourge of Armagon", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 28 * 1024 * 1024;
		games_found[index].status_phat = games_found[index].status_slim = GAME_STATUS_PERFECT;
		games_found[index].exec_id1_config = true;
		games_found[index].compat_mode = COMPAT_HIPNOTIC;
	}
	// Quake Mission Pack 2: Dissolution of Eternity
	else if (strcasecmp(dirname, "rogue") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*64));
		sprintf(games_found[index].pretty_name, "Quake Mission Pack 2: Dissolution of Eternity", dirname);
		games_found[index].heap_phat = 12 * 1024 * 1024;
		games_found[index].heap_slim = 28 * 1024 * 1024;
		games_found[index].status_phat = GAME_STATUS_DECENT;
		games_found[index].status_slim = GAME_STATUS_PERFECT;
		games_found[index].exec_id1_config = true;
		games_found[index].compat_mode = COMPAT_ROGUE;
	}
	// In The Shadows
	else if (strcasecmp(dirname, "shadows") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*16));
		sprintf(games_found[index].pretty_name, "In The Shadows", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 32 * 1024 * 1024;
		games_found[index].status_phat = GAME_STATUS_BAD;
		games_found[index].status_slim = GAME_STATUS_BAD;
	}
	// UltraQuake
	else if (strcasecmp(dirname, "ultraquake") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*16));
		sprintf(games_found[index].pretty_name, "UltraQuake", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 32 * 1024 * 1024;
		games_found[index].status_phat = GAME_STATUS_BAD;
		games_found[index].status_slim = GAME_STATUS_BAD;
		games_found[index].exec_id1_config = true;
	}
	// I WANT TOAST
	else if (strcasecmp(dirname, "toast") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*16));
		sprintf(games_found[index].pretty_name, "I WANT TOAST", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 20 * 1024 * 1024;
		games_found[index].status_phat = games_found[index].status_slim = GAME_STATUS_PERFECT;
		games_found[index].exec_id1_config = true;
	}
	// Abandon
	else if (strcasecmp(dirname, "abandon") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*16));
		sprintf(games_found[index].pretty_name, "Abandon", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 20 * 1024 * 1024;
		games_found[index].status_phat = games_found[index].status_slim = GAME_STATUS_PERFECT;
		games_found[index].exec_id1_config = true;
	}
	// Dimension of the Past
	else if (strcasecmp(dirname, "dopa") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*32));
		sprintf(games_found[index].pretty_name, "Dimension of the Past", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 24 * 1024 * 1024;
		games_found[index].status_phat = games_found[index].status_slim = GAME_STATUS_PERFECT;
		games_found[index].exec_id1_config = true;
	}
	// LibreQuake
	else if (strcasecmp(dirname, "lq1") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*16));
		sprintf(games_found[index].pretty_name, "LibreQuake", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 32 * 1024 * 1024;
		games_found[index].status_phat = GAME_STATUS_DECENT;
		games_found[index].status_slim = GAME_STATUS_DECENT;
		games_found[index].exec_id1_config = true;
	}
	// Kickflip Quake
	else if (strcasecmp(dirname, "skate") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*16));
		sprintf(games_found[index].pretty_name, "Kickflip Quake", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 20 * 1024 * 1024;
		games_found[index].status_phat = games_found[index].status_slim = GAME_STATUS_PERFECT;
	}
	// Quake Xmas Jam 2018
	else if (strcasecmp(dirname, "xmasjam2018") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*32));
		sprintf(games_found[index].pretty_name, "Quake Xmas Jam 2018", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 32 * 1024 * 1024;
		games_found[index].status_phat = games_found[index].status_slim = GAME_STATUS_BAD;
	}
	// NAVY SEALS
	else if (strcasecmp(dirname, "navyseal") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*16));
		sprintf(games_found[index].pretty_name, "NAVY SEALS", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 20 * 1024 * 1024;
		games_found[index].status_phat = games_found[index].status_slim = GAME_STATUS_BAD;
	}
	// Beyond Belief
	else if (strcasecmp(dirname, "bbelief") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*16));
		sprintf(games_found[index].pretty_name, "Beyond Belief", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 20 * 1024 * 1024;
		games_found[index].status_phat = games_found[index].status_slim = GAME_STATUS_PERFECT;
		games_found[index].exec_id1_config = true;
	}
	// Rubicon 2
	else if (strcasecmp(dirname, "rubicon2") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*16));
		sprintf(games_found[index].pretty_name, "Rubicon 2", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 32 * 1024 * 1024;
		games_found[index].status_phat = GAME_STATUS_BAD;
		games_found[index].status_slim = GAME_STATUS_DECENT;
		games_found[index].exec_id1_config = true;
	}
	// The Punishment Due
	else if (strcasecmp(dirname, "pun") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*32));
		sprintf(games_found[index].pretty_name, "The Punishment Due", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 32 * 1024 * 1024;
		games_found[index].status_phat = games_found[index].status_slim = GAME_STATUS_BAD;
	}
	// Terra
	else if (strcasecmp(dirname, "terra") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*8));
		sprintf(games_found[index].pretty_name, "Terra", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 20 * 1024 * 1024;
		games_found[index].status_phat = games_found[index].status_slim = GAME_STATUS_PERFECT;
		games_found[index].exec_id1_config = true;
	}
	// Honey
	else if (strcasecmp(dirname, "honey") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*8));
		sprintf(games_found[index].pretty_name, "Honey", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 32 * 1024 * 1024;
		games_found[index].status_phat = GAME_STATUS_BAD;
		games_found[index].status_slim = GAME_STATUS_DECENT;
		games_found[index].exec_id1_config = true;
	}
	// Air Quake I
	else if (strcasecmp(dirname, "airquake") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*16));
		sprintf(games_found[index].pretty_name, "Air Quake I", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 32 * 1024 * 1024;
		games_found[index].status_phat = GAME_STATUS_BAD;
		games_found[index].status_slim = GAME_STATUS_PERFECT;
	}
	// Quake I Arena
	else if (strcasecmp(dirname, "q1a") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*16));
		sprintf(games_found[index].pretty_name, "Quake I Arena", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 20 * 1024 * 1024;
		games_found[index].status_phat = GAME_STATUS_PERFECT;
		games_found[index].status_slim = GAME_STATUS_PERFECT;
		games_found[index].exec_id1_config = true;
	}
	// Slide
	else if (strcasecmp(dirname, "slide") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*8));
		sprintf(games_found[index].pretty_name, "Slide", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 20 * 1024 * 1024;
		games_found[index].status_phat = GAME_STATUS_BAD;
		games_found[index].status_slim = GAME_STATUS_BAD;
	}
	// X-MEN: Ravages of Apocalypse
	else if (strcasecmp(dirname, "xmen") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*32));
		sprintf(games_found[index].pretty_name, "X-MEN: Ravages of Apocalypse", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 32 * 1024 * 1024;
		games_found[index].status_phat = GAME_STATUS_BAD;
		games_found[index].status_slim = GAME_STATUS_BAD;
	}
	// Abyss of Pandemonium - The Final Mission
	else if (strcasecmp(dirname, "aop") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*64));
		sprintf(games_found[index].pretty_name, "Abyss of Pandemonium - The Final Mission", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 32 * 1024 * 1024;
		games_found[index].status_phat = GAME_STATUS_DECENT;
		games_found[index].status_slim = GAME_STATUS_PERFECT;
		games_found[index].exec_id1_config = true;
	}
	// SUPERHOT Quake
	else if (strcasecmp(dirname, "shquake") == 0) {
		games_found[index].has_pretty_name = true;
		games_found[index].pretty_name = static_cast<char*>(malloc(sizeof(char)*16));
		sprintf(games_found[index].pretty_name, "SUPERHOT Quake", dirname);
		games_found[index].heap_phat = 13 * 1024 * 1024;
		games_found[index].heap_slim = 32 * 1024 * 1024;
		games_found[index].status_phat = GAME_STATUS_PERFECT;
		games_found[index].status_slim = GAME_STATUS_PERFECT;
		games_found[index].compat_mode = COMPAT_SUPERHOT;
	}
}

int Random_Int (int max_int)
{
	float	f;
	f = (rand ()&0x7fff) / ((float)0x7fff) * max_int;
	if (f > 0)
		return (int)(f + 0.5) + 1;
	else
		return (int)(f - 0.5) + 1;
}

char* GenerateSplashText(void)
{
	int num = Random_Int(10);

	switch(num) {
		case 1: return "DO NOT PET THE SHAMBLER"; break;
		case 2: return "SHUB-NIGGURATH USES THE HARD R"; break;
		case 3: return "NOW WITCH CHUNKIER PIXELS"; break;
		case 4: return "erm...."; break;
		case 5: return "THE SHAMBLER HAS FUR"; break;
		case 6: return "THE SHAMBLER HAS NO FUR"; break;
		case 7: return "50\% compatibility on a good day!"; break;
		case 8: return "technically i'm saying anything"; break;
		case 9: return "new as of 2024!"; break;
		case 10: return "yeah! quake his ass!"; break;
		default: return "GPLv2 since '99"; break;
	}
}

#define TEXT_WHITE 	0xffffff
#define TEXT_RED   	0x0000ff
#define TEXT_GREEN 	0x00ff00
#define TEXT_BLUE  	0xff0000
#define TEXT_PURPLE	0xff00ff
#define TEXT_YELLOW 0xffff00

void StartUpParams(char **args, int argc, char *cmdlinePath, char *currentDirectory, char *gameDirectory) 
{
	// a REALLY quick and dirty interface thrown together to be
	// more user-accessible and list mod compatibility.
	if (CheckParm(args, f_argc,"-interface")) {
		SceCtrlData pad;
		pspDebugScreenInit();
	
		sceCtrlSetSamplingCycle(0);
		sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

		for(int i = 0; i < 25; i++) {
			games_found[i].occupied = false;
		}

		// Grab all of the mods we have
		int dir_fd = sceIoDopen(gameDirectory);
		int dir_index = 0;
		
 		if (dir_fd >= 0) {
 			SceIoDirent* p_dir_de = new SceIoDirent; 
 			int res = -1;
 			
 			do {
 				res = sceIoDread(dir_fd, p_dir_de);
 				if ((res > 0) && (p_dir_de->d_stat.st_attr & FIO_SO_IFDIR)) {
 					if (!(strcasecmp(p_dir_de->d_name, ".") == 0 || strcasecmp(p_dir_de->d_name, "..") == 0 ||
 					      strcasecmp(p_dir_de->d_name, "mp3") == 0 || strcasecmp(p_dir_de->d_name, "hooks") == 0 ||
						  strcasecmp(p_dir_de->d_name, "patches") == 0)) {
						register_game(p_dir_de->d_name, dir_index);
						dir_index++;
 					}
 				}
 			} while (res > 0);
 			delete p_dir_de;
 		}
		sceIoDclose(dir_fd);

		char* splash = GenerateSplashText();
		
		bool done = false;
		int cursor_index = 0;

		while(!done) {
			sceDisplayWaitVblankStart();
			sceDisplayWaitVblankStart();
			sceDisplayWaitVblankStart();
			sceDisplayWaitVblankStart();
			sceDisplayWaitVblankStart();
    		
			pspDebugScreenSetXY(0, 1);
			sceCtrlReadBufferPositive(&pad, 1); 
			
			pspDebugScreenSetTextColor(TEXT_WHITE);
			printf(" %s %s : \"%s\"\n", ENGINE_NAME, ENGINE_VERSION, splash);
			pspDebugScreenSetTextColor(TEXT_RED);
			printf("====================================================================\n");
			pspDebugScreenSetTextColor(TEXT_BLUE);
			printf("   Playing an unidentified mod will use heap from quake.cmdline   \n\n");
			pspDebugScreenSetTextColor(TEXT_WHITE);

			// 25 entries max..
			for(int i = 0; i < dir_index; i++) {
				if (games_found[i].has_pretty_name == true) {
					if (i == cursor_index)
						printf(" * %s ", games_found[i].pretty_name);
					else
						printf("  %s ", games_found[i].pretty_name);
				} else {
					if (i == cursor_index)
						printf(" * %s ", games_found[i].dir);
					else
						printf("  %s ", games_found[i].dir);
				}

				if (psp_system_model == PSP_MODEL_PHAT) {
					switch(games_found[i].status_phat) {
						case GAME_STATUS_UNKNOWN:
							pspDebugScreenSetTextColor(TEXT_PURPLE);
							printf("(UNKNOWN)\n");
							break;
						case GAME_STATUS_BAD:
							pspDebugScreenSetTextColor(TEXT_RED);
							printf("(NOT PLAYABLE)\n");
							break;
						case GAME_STATUS_DECENT:
							pspDebugScreenSetTextColor(TEXT_YELLOW);
							printf("(DECENT)\n");
							break;
						case GAME_STATUS_PERFECT:
							pspDebugScreenSetTextColor(TEXT_GREEN);
							printf("(GOOD)\n");
							break;
						default: break;
					}
					pspDebugScreenSetTextColor(TEXT_WHITE);
				} else {
					switch(games_found[i].status_slim) {
						case GAME_STATUS_UNKNOWN:
							pspDebugScreenSetTextColor(TEXT_PURPLE);
							printf("(UNKNOWN)\n");
							break;
						case GAME_STATUS_BAD:
							pspDebugScreenSetTextColor(TEXT_RED);
							printf("(NOT PLAYABLE)\n");
							break;
						case GAME_STATUS_DECENT:
							pspDebugScreenSetTextColor(TEXT_YELLOW);
							printf("(DECENT)\n");
							break;
						case GAME_STATUS_PERFECT:
							pspDebugScreenSetTextColor(TEXT_GREEN);
							printf("(GOOD)\n");
							break;
						default: break;
					}
					pspDebugScreenSetTextColor(TEXT_WHITE);
				}
			}

			pspDebugScreenSetXY(0, 32);
			pspDebugScreenSetTextColor(TEXT_BLUE);
			printf(" X: Select Title | HOME: Quit to XMB\n");
			
			if (pad.Buttons != 0) {
				// Start game
				if (pad.Buttons & PSP_CTRL_CROSS) {
					done = true;

					// Set Heap sizes
					if (psp_system_model == PSP_MODEL_PHAT) {
						heapSize = (size_t)games_found[cursor_index].heap_phat;
					} else {
						heapSize = (size_t)games_found[cursor_index].heap_slim;
					}

					if (games_found[cursor_index].exec_id1_config == true)
						exec_id1_config = qtrue;

					compat_gametype = games_found[cursor_index].compat_mode;

					// Don't do -game parm stuff if its id1 (fixes booting shareware)
					if (strcasecmp(games_found[cursor_index].dir, "id1") != 0) {
						// Set the game param to the mod of choice
						int len1 = strlen("-game");
						int len2 = strlen(games_found[cursor_index].dir);
						args[f_argc] = new char[len1+1];
						args[f_argc+1] = new char[len2+1];
							
						strcpy(args[f_argc], "-game");
						strcpy(args[f_argc+1], games_found[cursor_index].dir);	
				
						f_argc += 2;
					}

					// Clean up all that memory we allocated
					for (int i = 0; i < 25; i++) {
						if (games_found[i].dir != NULL)
							delete games_found[i].dir;
						if (games_found[i].pretty_name != NULL)
							delete games_found[i].pretty_name;
					}
				}
				if (pad.Buttons & PSP_CTRL_UP){
					cursor_index--;
					if (cursor_index < 0)
						cursor_index = dir_index - 1;				
				} 
				if (pad.Buttons & PSP_CTRL_DOWN){
					cursor_index++;
					if (cursor_index > dir_index - 1)
						cursor_index = 0;		
				} 
				if (pad.Buttons & PSP_CTRL_HOME) {
					Sys_Quit();
				}
			}
			
		}	
	}
}

SceUID vram_module;

void InitEdramModule(void)
{
	char currentDirectory[1024];
	char gameDirectory[1024];
	char path_f[256];

	memset(gameDirectory, 0, sizeof(gameDirectory));
	memset(currentDirectory, 0, sizeof(currentDirectory));
	getcwd(currentDirectory, sizeof(currentDirectory) - 1);

	strcpy(path_f, currentDirectory);
	strcat(path_f, "/hooks/vramext.prx");
	
	vram_module = pspSdkLoadStartModule(path_f, PSP_MEMORY_PARTITION_KERNEL);
	if (vram_module >= 0 && psp_system_model != PSP_MODEL_PHAT) {
		VramSetSize(4096);
	}
}

void ShutdownEdramModule(void)
{
	if (vram_module < 0)
		return;

	sceKernelStopModule(vram_module, 0, 0, 0, 0);
	sceKernelUnloadModule(vram_module);
}

int main(int argc, char *argv[])		
{
#ifdef KERNEL_MODE
	// Load the network modules

    	// create user thread, tweek stack size here if necessary
    	SceUID thid = sceKernelCreateThread("User Mode Thread", user_main,
            0x20, // default priority
            512 * 1024, // stack size (256KB is regular default)
            PSP_THREAD_ATTR_USBWLAN | PSP_THREAD_ATTR_USER  | PSP_THREAD_ATTR_VFPU , NULL);

	// start user thread, then wait for it to do everything else
	sceKernelStartThread(thid, 0, NULL);
	sceKernelWaitThreadEnd(thid, NULL);

	sceKernelExitGame();
}

int user_main(SceSize argc, void* argp)
	{
#endif	
	// Set up the callback thread, this is not appropriate for use with
	// the loader, so don't bother calling it as it apparently seems to
	// cause problems with firmware 2.0+
	//setUpCallbackThread(); //<-- dark_duke removal 7/6/2016

	srand(time(NULL));
	psp_system_model = Sys_GetPSPModel();

	// Disable floating point exceptions.
	// If this isn't done, Quake crashes from (presumably) divide by zero
	// operations.
	disableFloatingPointExceptions();

	// Enable full VRAM access on non-PHAT model units
	InitEdramModule();

	// Get the current working dir.
	char currentDirectory[1024];
	char gameDirectory[1024];

	memset(gameDirectory, 0, sizeof(gameDirectory));
	memset(currentDirectory, 0, sizeof(currentDirectory));
	getcwd(currentDirectory, sizeof(currentDirectory) - 1);

	char   path_f[256];
	strcpy(path_f,currentDirectory);
	strcat(path_f,"/quake.cmdline");
	Sys_ReadCommandLineFile(path_f);

	char *args[MAX_NUM_ARGVS];
	
	for (int k =0; k < f_argc; k++) {
		int len = strlen(f_argv[k]);
		args[k] = new char[len+1];
		strcpy(args[k], f_argv[k]);
	}
	
	if (CheckParm(args, f_argc, "-gamedir")) {
		char* tempStr = args[CheckParm(args, f_argc,"-gamedir")+1]; 
		strncpy(gameDirectory, tempStr, sizeof(gameDirectory)-1);
	}
	else
	{
		strncpy(gameDirectory,currentDirectory,sizeof(gameDirectory)-1);
	}

	/////
	StartUpParams(args, f_argc, path_f, currentDirectory, gameDirectory);
	setUpCallbackThread();
	
	if (CheckParm(args, f_argc, "-linear")) {
		char* tempStr = args[CheckParm(args, f_argc,"-linear")+1];
	}
		
	if (CheckParm(args, f_argc, "-nearest")) {
		char* tempStr = args[CheckParm(args, f_argc,"-nearest")+1];
	}
	
	if (CheckParm(args, f_argc, "-modmusic")) {
	char* tempStr = args[CheckParm(args, f_argc,"-modmusic")+1];
	}
		
	if (CheckParm(args, f_argc, "-heap")) {
		char* tempStr = args[CheckParm(args, f_argc,"-heap")+1]; 
		int heapSizeMB = atoi(tempStr);
		
		if (heapSizeMB < MIN_HEAP_MB )
			heapSizeMB = MIN_HEAP_MB;
		
		if (heapSizeMB > MAX_HEAP_MB )
			heapSizeMB = MAX_HEAP_MB;
			
		heapSize = heapSizeMB * 1024 * 1024;
	}

	if (heapSize == 0) {
		if (psp_system_model == PSP_MODEL_PHAT)
			heapSize = 13 * 1024 * 1024;
		else
			heapSize = 32 * 1024 * 1024;
	}
	
	// Allocate the heap.
	std::vector<unsigned char>	heap(heapSize, 0);

#if CONSOLE_DEBUG
	if (f_argc > 1) {
		args[f_argc++] = "-condebug";
		COM_InitArgv(f_argc, args);
	}
	else {
		args[0] = "";
		args[1] = "-condebug";
		COM_InitArgv(2, args);
	}
#else
	if (f_argc > 1)
		COM_InitArgv(f_argc, args);
	else {
		args[0] = "";
		COM_InitArgv(0, NULL);
	}
#endif

	// cypress -- always use 333MHz, fuck it!
	scePowerSetClockFrequency(333, 333, 166);

	if (COM_CheckParm("-gamedir")) {
		char* tempStr = com_argv[COM_CheckParm("-gamedir")+1];
		strncpy(gameDirectory, tempStr, sizeof(gameDirectory)-1);
	}
	else
	{
		strncpy(gameDirectory,currentDirectory,sizeof(gameDirectory)-1);
	}
	// Catch exceptions from here.
	try
	{
		// Initialise the Host module.
		quakeparms_t parms;
		memset(&parms, 0, sizeof(parms));
		parms.argc		= com_argc;
		parms.argv		= com_argv;
		parms.basedir	= gameDirectory;
		parms.memsize	= heap.size();
		parms.membase	= &heap.at(0);
		Host_Init(&parms);

		// Precalculate the tick rate.
		const float oneOverTickRate = 1.0f / sceRtcGetTickResolution();

		// Record the time that the main loop started.
		u64 lastTicks;
		sceRtcGetCurrentTick(&lastTicks);

		// Enter the main loop.

#ifdef PSP_MP3HARDWARE_MP3LIB
		extern int changeMp3Volume;
		extern void CDAudio_VolumeChange(float bgmvolume);
#endif
		while (!quit)
		{

#ifdef PSP_MP3HARDWARE_MP3LIB
			if(changeMp3Volume) CDAudio_VolumeChange(bgmvolume.value);
#endif

			// Handle suspend & resume.
			if (suspended)
			{
				// Suspend.
				S_ClearBuffer();

				quake::system::suspend();

				// Wait for resume.
				while (suspended && !quit)
				{
					sceDisplayWaitVblankStart();
				}

				// Resume.
				quake::system::resume();

				// Reset the clock.
				sceRtcGetCurrentTick(&lastTicks);
			}

			// What is the time now?
			u64 ticks;
			sceRtcGetCurrentTick(&ticks);

			// How much time has elapsed?
			const unsigned int	deltaTicks		= ticks - lastTicks;
			const float			deltaSeconds	= deltaTicks * oneOverTickRate;

			// Check the battery status.
			battery::check();

			// Run the frame.
			Host_Frame(deltaSeconds);

			// Remember the time for next frame.
			lastTicks = ticks;
		}
	}
	catch (const std::exception& e)
	{
		// Report the error and quit.
		Sys_Error("C++ Exception: %s", e.what());
		return 0;
	}

	// Quit.
	ShutdownEdramModule();
	Sys_Quit();
	return 0;
}

void Sys_ReadCommandLineFile (char* netpath)
{
	int 	in;
	int     remaining, count;
	char    buf[4096];
	int     argc = 1;

	remaining = Sys_FileOpenRead (netpath, &in);

	if (in > 0 && remaining > 0) {
		count = Sys_FileRead (in, buf, 4096);
		f_argv[0] = empty_string;

		char* lpCmdLine = buf;

		while (*lpCmdLine && (argc < MAX_NUM_ARGVS))
		{
			while (*lpCmdLine && ((*lpCmdLine <= 32) || (*lpCmdLine > 126)))
				lpCmdLine++;

			if (*lpCmdLine)
			{
				f_argv[argc] = lpCmdLine;
				argc++;

				while (*lpCmdLine && ((*lpCmdLine > 32) && (*lpCmdLine <= 126)))
					lpCmdLine++;

				if (*lpCmdLine)
				{
					*lpCmdLine = 0;
					lpCmdLine++;
				}
			}
		}
		f_argc = argc;
	} else {
		f_argc = 0;
	}

	if (in > 0)
		Sys_FileClose (in);
}