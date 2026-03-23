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

#include "dos_inc.h"

#include "../src/dos/dev_lpt.cpp"

#include <cstdio>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace {

class LptTest : public ::testing::Test {
protected:
	void SetUp() override {
		test_filename = "test_lpt_output.prn";
		test_name = "LPT1";
	}

	void TearDown() override {
		std::remove(test_filename.c_str());
	}

	std::string test_filename;
	std::string test_name;
};

TEST_F(LptTest, ConstructionWithDisabledCmd) {
	device_LPT lpt(test_name.c_str(), test_filename.c_str(), nullptr);

 EXPECT_EQ(lpt.GetName(), test_name);
}

TEST_F(LptTest, ConstructionWithCommand) {
	const char* cmd = "cat %s";
	device_LPT lpt(test_name.c_str(), test_filename.c_str(), cmd);

 EXPECT_EQ(lpt.GetName(), test_name);
}

TEST_F(LptTest, ReadReturnsZero) {
	device_LPT lpt(test_name.c_str(), test_filename.c_str(), nullptr);

	uint8_t data[16] = {};
	uint16_t size = sizeof(data);

	const bool result = lpt.Read(data, &size);

 EXPECT_TRUE(result);
 EXPECT_EQ(size, 0);
}

TEST_F(LptTest, WriteCreatesFile) {
	device_LPT lpt(test_name.c_str(), test_filename.c_str(), nullptr);

	const uint8_t data[] = "Hello, Printer!";
	uint16_t size = sizeof(data) - 1;

	const bool result = lpt.Write(const_cast<uint8_t*>(data), &size);

	lpt.Close();

 EXPECT_TRUE(result);
 std::ifstream file(test_filename);
 ASSERT_TRUE(file.is_open());
 std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
 EXPECT_EQ(content, "Hello, Printer!");
}

TEST_F(LptTest, WriteMultipleTimesAppends) {
	device_LPT lpt(test_name.c_str(), test_filename.c_str(), nullptr);

	const uint8_t data1[] = "First";
	uint16_t size1 = sizeof(data1) - 1;
	lpt.Write(const_cast<uint8_t*>(data1), &size1);

	lpt.Close();

	const uint8_t data2[] = "Second";
	uint16_t size2 = sizeof(data2) - 1;
	lpt.Write(const_cast<uint8_t*>(data2), &size2);

	lpt.Close();

	std::ifstream file(test_filename);
 std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
 EXPECT_EQ(content, "FirstSecond");
}

TEST_F(LptTest, SeekReturnsTrue) {
	device_LPT lpt(test_name.c_str(), test_filename.c_str(), nullptr);

	uint32_t pos = 0;
	const bool result = lpt.Seek(&pos, 0);

 EXPECT_TRUE(result);
}

TEST_F(LptTest, CloseClosesFileHandle) {
	device_LPT lpt(test_name.c_str(), test_filename.c_str(), nullptr);

	const uint8_t data[] = "Test";
	uint16_t size = sizeof(data) - 1;
	lpt.Write(const_cast<uint8_t*>(data), &size);

	lpt.Close();

	lpt.Close();
}

TEST_F(LptTest, GetInformationReturnsDeviceFlag) {
	device_LPT lpt(test_name.c_str(), test_filename.c_str(), nullptr);

	const uint16_t info = lpt.GetInformation();

 EXPECT_EQ(info, 0x8000);
}

} // namespace
