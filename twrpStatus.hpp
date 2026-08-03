/*
	Copyright 2026 TeamWin
	This file is part of TWRP/TeamWin Recovery Project.

	TWRP is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	TWRP is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _TWRPSTATUS_HPP
#define _TWRPSTATUS_HPP

#include <string>

class TWStatus
{
public:
	enum Status {
		STATUS_BOOT,      // recovery is still starting up
		STATUS_IDLE,      // ready and waiting, nothing running
		STATUS_BUSY,      // an operation is running
		STATUS_SIDELOAD,  // waiting for a package over adb sideload
		STATUS_SUCCESS,   // the outermost operation finished successfully
		STATUS_ERROR      // the outermost operation failed
	};

	static void Set(Status status);
	static void Ready();
	static void Operation_Start(const std::string& name);
	static void Operation_End(int operation_status);
	static void Set_Progress(int percent);

private:
	static const char* Status_Name(Status status);
	static std::string Normalize(const std::string& name);
	static void Publish();
	static void Run_Hook(const char* status, const char* operation, const char* progress);
};

#endif // _TWRPSTATUS_HPP
