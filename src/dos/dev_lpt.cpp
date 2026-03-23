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

#include "dev_lpt.h"

#include <cstdio>
#include <cstring>

#include "dosbox.h"

#include <SDL.h>

static std::queue<PrintJob> print_queue;
static SDL_mutex* queue_mutex = nullptr;
static SDL_Thread* worker_thread = nullptr;
static bool shutdown_flag = false;

static int PrintWorkerThread(void*)
{
	while (!shutdown_flag) {
		PrintJob job;
		bool has_job = false;

		SDL_LockMutex(queue_mutex);

		if (!print_queue.empty()) {
			job = print_queue.front();
			print_queue.pop();
			has_job = true;
		}

		SDL_UnlockMutex(queue_mutex);

		if (has_job) {
			int result = system(job.cmd.c_str());
			if (result == -1) {
				LOG_MSG("%s: Error executing: %s",
				        job.device_name.c_str(),
				        job.cmd.c_str());
			}
			unlink(job.filename.c_str());
		} else {
			SDL_Delay(100); // 100ms
		}
	}
	return 0;
}

void LPT_InitPrintQueue()
{
	queue_mutex = SDL_CreateMutex();
	shutdown_flag = false;
	worker_thread = SDL_CreateThread(PrintWorkerThread, "LPT Printer Spooler", nullptr);
}

void LPT_ShutdownPrintQueue()
{
	shutdown_flag = true;

	SDL_LockMutex(queue_mutex);

	while (!print_queue.empty()) {
		auto job = print_queue.front();
		print_queue.pop();

		SDL_UnlockMutex(queue_mutex);

		LOG_MSG("%s: Processing queued job: %s", job.device_name.c_str(), job.cmd.c_str());
		int result = system(job.cmd.c_str());
		if (result == -1) {
			LOG_MSG("%s: Error executing: %s", job.device_name.c_str(), job.cmd.c_str());
		}
		unlink(job.filename.c_str());

		SDL_LockMutex(queue_mutex);
	}

	SDL_UnlockMutex(queue_mutex);

	if (worker_thread) {
		SDL_WaitThread(worker_thread, nullptr);
		worker_thread = nullptr;
	}

	SDL_DestroyMutex(queue_mutex);
	queue_mutex = nullptr;
}

device_LPT::device_LPT(const char* name, const char* fname, const char* ncmd)
{
	SetName(name);
	strncpy(filename, fname, CROSS_LEN - 1);
	filename[CROSS_LEN - 1] = 0;
	if (ncmd) {
		cmd = ncmd;
	}
	handle = nullptr;
	last_access = 0;
}

bool device_LPT::Read(uint8_t* /*data*/, uint16_t* size)
{
	*size = 0;
	return true;
}

bool device_LPT::Write(uint8_t* data, uint16_t* size)
{
	if (!handle) {
		handle = fopen(filename, "ab");
		if (!handle) {
			return false;
		}
	}
	return fwrite(data, 1, *size, handle) == *size;
}

bool device_LPT::Seek(uint32_t* /*pos*/, uint32_t /*type*/)
{
	return true;
}

bool device_LPT::Close()
{
	if (handle) {
		fclose(handle);
		handle = nullptr;
		last_access = GetTicks();
	}
	return true;
}

void device_LPT::Flush(uint32_t timeout)
{
	char work[CROSS_LEN];
	int64_t ticks = GetTicks();

	if (last_access == 0 || handle || ticks - last_access < timeout) {
		return;
	}

	last_access = 0;

	if (cmd.empty()) {
		LOG_MSG("Output to %s discarded due to configuration settings", GetName());
	} else {
		snprintf(work, CROSS_LEN, cmd.c_str(), filename);

		PrintJob job;
		job.cmd = work;
		job.filename = filename;
		job.device_name = GetName();

		SDL_LockMutex(queue_mutex);
		print_queue.push(job);
		SDL_UnlockMutex(queue_mutex);

		LOG_MSG("%s: %s queued", GetName(), filename);
	}
}

uint16_t device_LPT::GetInformation()
{
	return 0x8000;
}

device_LPT::~device_LPT()
{
	Flush(0);
}
