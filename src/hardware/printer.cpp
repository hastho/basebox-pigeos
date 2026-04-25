/*
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 *  Copyright (C) 2020-2024  The DOSBox Staging Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "dosbox.h"

#include <cstring>

#include "control.h"
#include "setup.h"
#include "timer.h"

#include "../dos/dev_lpt.h"

#define MAX_PRINTER_DEVICES 4

static uint32_t Timeout = 0;
static device_LPT* dev_lpt[MAX_PRINTER_DEVICES] = {};
static bool spooler_registered = false;

static void Spooler()
{
	for (int i = 0; i < MAX_PRINTER_DEVICES; i++) {
		if (dev_lpt[i]) {
			dev_lpt[i]->Flush(Timeout);
		}
	}
}

class PRINTERDEV final : public Module_base {
public:
	PRINTERDEV(Section* configuration) : Module_base(configuration)
	{
		char name[5] = {};
		char tmpdir[CROSS_LEN] = {};
		char work[CROSS_LEN] = {};
		const char* cmd = nullptr;
		const char* path = nullptr;
		int i = 0;

		for (i = 0; i < MAX_PRINTER_DEVICES; i++) {
			dev_lpt[i] = nullptr;
		}

		Section_prop* section = static_cast<Section_prop*>(configuration);
		Timeout = section->Get_int("print_timeout");
		if (!Timeout) {
			return;
		}

		TIMER_AddTickHandler(&Spooler);
		spooler_registered = true;

		path = section->Get_string("tmpdir").c_str();
		if (!path || !strlen(path)) {
			path = ".";
		}
		strncpy(tmpdir, path, CROSS_LEN - 1);
		tmpdir[CROSS_LEN - 1] = 0;

		size_t len = strlen(tmpdir);
		if (len > 0 && tmpdir[len - 1] != '/' && tmpdir[len - 1] != '\\') {
			if (len < CROSS_LEN - 1) {
				tmpdir[len] = '/';
				tmpdir[len + 1] = 0;
			}
		}

		for (i = 0; i < MAX_PRINTER_DEVICES; i++) {
			snprintf(name, sizeof(name), "LPT%d", i + 1);
			cmd = section->Get_string(name).c_str();
			if (!strcmp(cmd, "disabled")) {
				cmd = nullptr;
			}
			snprintf(work, CROSS_LEN, "%sdev%s.prn", tmpdir, name);
			dev_lpt[i] = new device_LPT(name, work, cmd);
			DOS_AddDevice(dev_lpt[i]);

			if (cmd) {
				LOG_MSG("LPT%d: %s", i + 1, cmd);
			} else {
				LOG_MSG("LPT%d: disabled", i + 1);
			}
		}
	}

	~PRINTERDEV()
	{
		if (spooler_registered) {
			TIMER_DelTickHandler(&Spooler);
			spooler_registered = false;
		}
		for (int i = 0; i < MAX_PRINTER_DEVICES; i++) {
			if (dev_lpt[i]) {
				DOS_DelDevice(dev_lpt[i]);
				delete dev_lpt[i];
				dev_lpt[i] = nullptr;
			}
		}
	}
};

static PRINTERDEV* PrinterBaseClass = nullptr;

void PRINTER_Shutdown([[maybe_unused]] Section* sec)
{
	LPT_ShutdownPrintQueue();
	delete PrinterBaseClass;
	PrinterBaseClass = nullptr;
}

void PRINTER_Init(Section* sec)
{
	LPT_InitPrintQueue();

	if (PrinterBaseClass) {
		delete PrinterBaseClass;
	}
	PrinterBaseClass = new PRINTERDEV(sec);
	sec->AddDestroyFunction(&PRINTER_Shutdown);
}