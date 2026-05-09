/*
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 *  Copyright (C) 2024-2024  The DOSBox Staging Team
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

#include "dos_inc.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include <gtest/gtest.h>

#include "control.h"
#include "dos_system.h"
#include "drives.h"
#include "shell.h"
#include "string_utils.h"
#include "support.h"

#include "dosbox_test_fixture.h"

namespace {

class LocalDriveTest : public DOSBoxTestFixture {
protected:
	std::string test_dir = {};

	void SetUp() override
	{
		DOSBoxTestFixture::SetUp();

		char dir_template[] = "/tmp/dosbox_localdrive_XXXXXX";
		char* result = mkdtemp(dir_template);
		ASSERT_NE(result, nullptr);
		test_dir = result;
	}

	void TearDown() override
	{
		if (!test_dir.empty()) {
			const std::string cmd = "rm -rf " + test_dir;
			std::system(cmd.c_str());
		}
		DOSBoxTestFixture::TearDown();
	}

	void CreateHostFile(const std::string& name, const std::string& content)
	{
		const auto path = test_dir + "/" + name;
		std::FILE* file = std::fopen(path.c_str(), "w");
		ASSERT_NE(file, nullptr);
		std::fputs(content.c_str(), file);
		std::fclose(file);
	}
};

TEST_F(LocalDriveTest, ManualRefresh_DoesNotSeeHostChanges)
{
	CreateHostFile("file1.txt", "test");

	auto drive = std::make_shared<localDrive>(
	        test_dir.c_str(), 512, 32, 32765, 16000, 0xF8,
	        false, false, RefreshMode::Manual);
	Drives.at(drive_index('D')) = drive;

	dos.errorcode = DOSERR_NONE;
	EXPECT_TRUE(DOS_FindFirst("D:\\FILE1.TXT", 0, false));
	EXPECT_EQ(dos.errorcode, DOSERR_NONE);

	CreateHostFile("file2.txt", "test2");

	dos.errorcode = DOSERR_NONE;

	bool found_file2 = DOS_FindFirst("D:\\FILE2.TXT", 0, false);

	EXPECT_FALSE(found_file2) << "Manual refresh should cache directory, "
	                             "file2.txt should NOT be found";

	Drives.at(drive_index('D')) = nullptr;
}

TEST_F(LocalDriveTest, LazyRefresh_SeesHostChanges)
{
	CreateHostFile("file1.txt", "test");

	auto drive = std::make_shared<localDrive>(
	        test_dir.c_str(), 512, 32, 32765, 16000, 0xF8,
	        false, false, RefreshMode::Lazy);
	Drives.at(drive_index('E')) = drive;

	dos.errorcode = DOSERR_NONE;
	EXPECT_TRUE(DOS_FindFirst("E:\\FILE1.TXT", 0, false));
	EXPECT_EQ(dos.errorcode, DOSERR_NONE);

	CreateHostFile("file2.txt", "test2");

	dos.errorcode = DOSERR_NONE;

	bool found_file2 = DOS_FindFirst("E:\\FILE2.TXT", 0, false);

	EXPECT_TRUE(found_file2) << "Lazy refresh should invalidate cache on "
	                            "FindFirst, file2.txt SHOULD be found";
	EXPECT_EQ(dos.errorcode, DOSERR_NONE);

	Drives.at(drive_index('E')) = nullptr;
}

} // namespace
