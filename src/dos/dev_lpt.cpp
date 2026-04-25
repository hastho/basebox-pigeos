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
#include <cstdlib>
#include <cstring>
#include <vector>

#include "dosbox.h"

#include <SDL.h>

constexpr uint32_t QueueCheckIntervalMs = 100;

static std::queue<PrintJob> print_queue;
static SDL_mutex* queue_mutex = nullptr;
static SDL_Thread* worker_thread = nullptr;
static std::atomic<bool> shutdown_flag(false);

static int PrintWorkerThread(void*)
{
	while (!shutdown_flag.load()) {
		if (!queue_mutex) {
			SDL_Delay(QueueCheckIntervalMs);
			continue;
		}

		SDL_LockMutex(queue_mutex);

		if (!print_queue.empty()) {
			auto job = print_queue.front();
			print_queue.pop();

			SDL_UnlockMutex(queue_mutex);

			int result = system(job.cmd.c_str());
			if (result == -1) {
				LOG_MSG("%s: Error executing: %s",
				        job.device_name.c_str(),
				        job.cmd.c_str());
			}
			remove(job.filename.c_str());
		} else {
			SDL_UnlockMutex(queue_mutex);
			SDL_Delay(QueueCheckIntervalMs);
		}
	}
	return 0;
}

void LPT_InitPrintQueue()
{
	queue_mutex = SDL_CreateMutex();
	if (!queue_mutex) {
		LOG_MSG("LPT: Failed to create mutex");
		return;
	}
	shutdown_flag.store(false);
	worker_thread = SDL_CreateThread(PrintWorkerThread, "LPT Printer Spooler", nullptr);
	if (!worker_thread) {
		LOG_MSG("LPT: Failed to create worker thread");
		SDL_DestroyMutex(queue_mutex);
		queue_mutex = nullptr;
	}
}

void LPT_ShutdownPrintQueue()
{
	shutdown_flag.store(true);

	if (worker_thread) {
		SDL_WaitThread(worker_thread, nullptr);
		worker_thread = nullptr;
	}

	if (queue_mutex) {
		std::vector<PrintJob> remaining_jobs;

		SDL_LockMutex(queue_mutex);
		while (!print_queue.empty()) {
			remaining_jobs.push_back(print_queue.front());
			print_queue.pop();
		}
		SDL_UnlockMutex(queue_mutex);

		for (const auto& job : remaining_jobs) {
			LOG_MSG("%s: Processing queued job: %s",
			        job.device_name.c_str(),
			        job.cmd.c_str());
			int result = system(job.cmd.c_str());
			if (result == -1) {
				LOG_MSG("%s: Error executing: %s",
				        job.device_name.c_str(),
				        job.cmd.c_str());
			}
			remove(job.filename.c_str());
		}

		SDL_DestroyMutex(queue_mutex);
		queue_mutex = nullptr;
	}
}

device_LPT::device_LPT(const char* name, const char* fname, const char* ncmd)
{
	SetName(name);
	filename = fname;
	if (ncmd) {
		cmd = ncmd;
		cmd_valid = (cmd.find("%s") != std::string::npos);
	}
	handle = nullptr;
	last_access = 0;
	write_failed = false;
}

bool device_LPT::Read(uint8_t* /*data*/, uint16_t* size)
{
	*size = 0;
	return true;
}

bool device_LPT::Write(uint8_t* data, uint16_t* size)
{
	if (write_failed) {
		return false;
	}
	if (!handle) {
		handle = fopen(filename.c_str(), "ab");
		if (!handle) {
			write_failed = true;
			LOG_MSG("%s: Failed to open %s for writing",
			        GetName(),
			        filename.c_str());
			return false;
		}
	}
	if (fwrite(data, 1, *size, handle) != *size) {
		write_failed = true;
		LOG_MSG("%s: Write failed to %s", GetName(), filename.c_str());
		return false;
	}
	return true;
}

bool device_LPT::Seek(uint32_t* /*pos*/, uint32_t /*type*/)
{
	return true;
}

void device_LPT::Close()
{
	if (handle) {
		fclose(handle);
		handle = nullptr;
		last_access = GetTicks();
		write_failed = false;
	}
}

void device_LPT::Flush(uint32_t timeout)
{
	int64_t ticks = GetTicks();

	if (last_access == 0 || handle || ticks - last_access < timeout) {
		return;
	}

	last_access = 0;

	if (cmd.empty() || !cmd_valid) {
		LOG_MSG("Output to %s discarded: invalid command '%s'",
		        GetName(),
		        cmd.c_str());
	} else {
		char work[1024];
		snprintf(work, sizeof(work), cmd.c_str(), filename.c_str());

		PrintJob job;
		job.cmd = work;
		job.filename = filename;
		job.device_name = GetName();

		if (queue_mutex) {
			SDL_LockMutex(queue_mutex);
			print_queue.push(job);
			SDL_UnlockMutex(queue_mutex);
		}

		LOG_MSG("%s: %s queued", GetName(), filename.c_str());
	}
}

uint16_t device_LPT::GetInformation()
{
	return 0x8000;
}

device_LPT::~device_LPT()
{
	Close();
	Flush(0);
}