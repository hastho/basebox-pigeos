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

#ifndef DOSBOX_DEV_LPT_H
#define DOSBOX_DEV_LPT_H

#include "dos_system.h"
#include "timer.h"

#include <queue>
#include <string>

struct PrintJob {
	std::string cmd = {};
	std::string filename = {};
	std::string device_name = {};
};

class device_LPT final : public DOS_Device {
public:
	device_LPT(const char* name, const char* fname, const char* ncmd);

	device_LPT(const device_LPT&) = delete;
	device_LPT& operator=(const device_LPT&) = delete;

	bool Read(uint8_t* data, uint16_t* size) override;
	bool Write(uint8_t* data, uint16_t* size) override;
	bool Seek(uint32_t* pos, uint32_t type) override;
	bool Close() override;
	void Flush(uint32_t timeout);
	uint16_t GetInformation() override;
	~device_LPT() override;

private:
	char filename[CROSS_LEN] = {};
	std::string cmd = {};
	int64_t last_access = 0;
	FILE* handle = nullptr;
};

void LPT_InitPrintQueue();
void LPT_ShutdownPrintQueue();

#endif
