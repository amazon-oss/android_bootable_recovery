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

#include <pthread.h>
#include <stdio.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "cutils/properties.h"
#include "twcommon.h"
#include "twrpStatus.hpp"

#define TW_STATUS_PROP    "twrp.status"
#define TW_OPERATION_PROP "twrp.operation"
#define TW_PROGRESS_PROP  "twrp.progress"

// Progress is updated constantly during an install and every update costs a
// round trip to the property service, so only publish whole steps.
#define TW_PROGRESS_STEP 5

// ui_progress_frames is handed to us as a frame count that assumes the GUI
// renders at this rate, so it is the rate we have to play it back at.
#define TW_PROGRESS_FPS 48

// How often the slide is advanced. Publishing is rate limited by
// TW_PROGRESS_STEP anyway, so there is no point in waking up 48 times a second
// to move a twelve pixel LED ring.
#define TW_SLIDE_INTERVAL_MS 100

static pthread_mutex_t status_lock = PTHREAD_MUTEX_INITIALIZER;
static TWStatus::Status current_status = TWStatus::STATUS_BOOT;
static std::vector<std::string> operation_stack;
static int current_progress = 0;
static bool operation_failed = false;

// Mirrors GUIProgressBar's mSlide/mSlideInc/mSlideFrames. slide_portion is the
// amount of progress the pending portion is worth, which is added to
// slide_position over slide_frames frames once we are told how long it lasts.
static float slide_portion = 0.0f;
static float slide_position = 0.0f;
static float slide_increment = 0.0f;
static long slide_frames = 0;
static bool slide_thread_started = false;

const char* TWStatus::Status_Name(Status status)
{
	switch (status) {
		case STATUS_BOOT:     return "boot";
		case STATUS_IDLE:     return "idle";
		case STATUS_BUSY:     return "busy";
		case STATUS_SIDELOAD: return "sideload";
		case STATUS_SUCCESS:  return "success";
		case STATUS_ERROR:    return "error";
	}
	return "idle";
}

// Operation names come from the GUI where they are free form and meant to be
// read by a human ("Fix Contexts"). Property values are neither, so reduce
// them to something an init trigger can be written against.
std::string TWStatus::Normalize(const std::string& name)
{
	std::string normalized;

	for (size_t i = 0; i < name.size() && normalized.size() < (size_t)(PROPERTY_VALUE_MAX - 1); i++) {
		char c = name[i];
		if (c >= 'A' && c <= 'Z')
			normalized += (char)(c - 'A' + 'a');
		else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
			normalized += c;
		else if (!normalized.empty() && normalized[normalized.size() - 1] != '_')
			normalized += '_';
	}
	while (!normalized.empty() && normalized[normalized.size() - 1] == '_')
		normalized.erase(normalized.size() - 1);

	return normalized.empty() ? "unknown" : normalized;
}

void TWStatus::Run_Hook(const char* status, const char* operation, const char* progress)
{
#ifndef TW_STATUS_HOOK
	(void)status;
	(void)operation;
	(void)progress;
#else
	static const char* hook = EXPAND(TW_STATUS_HOOK);

	pid_t pid = fork();
	if (pid == 0) {
		// Fork twice so that we never have to wait on the hook itself and
		// never leave a zombie behind either. Only exec between the two, the
		// lock we are holding is still locked in here.
		if (fork() == 0) {
			execl(hook, hook, status, operation, progress, (char*)NULL);
			_exit(127);
		}
		_exit(0);
	} else if (pid > 0) {
		waitpid(pid, NULL, 0);
	}
#endif
}

void TWStatus::Publish()
{
#ifdef TW_STATUS_NOTIFY
	const char* status = Status_Name(current_status);
	std::string operation = operation_stack.empty() ? "none" : operation_stack.back();
	char progress[PROPERTY_VALUE_MAX];

	snprintf(progress, sizeof(progress), "%d", current_progress);

	// Status goes last so that whatever reacts to it already sees the
	// operation and progress that belong to it.
	property_set(TW_OPERATION_PROP, operation.c_str());
	property_set(TW_PROGRESS_PROP, progress);
	property_set(TW_STATUS_PROP, status);

	LOGINFO("status: %s operation=%s progress=%s\n", status, operation.c_str(), progress);

	Run_Hook(status, operation.c_str(), progress);
#endif
}

void TWStatus::Set(Status status)
{
	pthread_mutex_lock(&status_lock);
	current_status = status;
	Publish();
	pthread_mutex_unlock(&status_lock);
}

void TWStatus::Ready()
{
	pthread_mutex_lock(&status_lock);
	// Something may already have run during startup, an ORS script from
	// /cache for instance, and its result is more interesting than "idle".
	if (current_status == STATUS_BOOT) {
		current_status = STATUS_IDLE;
		Publish();
	}
	pthread_mutex_unlock(&status_lock);
}

void TWStatus::Operation_Start(const std::string& name)
{
	pthread_mutex_lock(&status_lock);
	if (operation_stack.empty())
		operation_failed = false;
	operation_stack.push_back(Normalize(name));
	current_status = STATUS_BUSY;
	current_progress = 0;
	slide_frames = 0;
	slide_increment = 0.0f;
	slide_portion = 0.0f;
	slide_position = 0.0f;
	Publish();
	pthread_mutex_unlock(&status_lock);
}

void TWStatus::Operation_End(int operation_status)
{
	pthread_mutex_lock(&status_lock);

	// Failures are sticky for as long as the outermost operation is running.
	// A zip that failed to install inside an otherwise successful script is
	// still a failure, and on a device whose only output is an LED it is much
	// better to over-report that than to flash green and reboot.
	if (operation_status != 0)
		operation_failed = true;

	// Whatever the portion still had left is moot now that it is over.
	slide_frames = 0;
	slide_increment = 0.0f;
	slide_portion = 0.0f;

	if (!operation_stack.empty())
		operation_stack.pop_back();

	if (operation_stack.empty()) {
		current_status = operation_failed ? STATUS_ERROR : STATUS_SUCCESS;
		current_progress = 100;
	}

	Publish();
	pthread_mutex_unlock(&status_lock);
}

void TWStatus::Set_Progress_Locked(int percent)
{
	if (percent < 0)
		percent = 0;
	else if (percent > 100)
		percent = 100;

	if (current_status == STATUS_BUSY &&
			percent / TW_PROGRESS_STEP != current_progress / TW_PROGRESS_STEP) {
		current_progress = percent;
		Publish();
	}
}

void TWStatus::Set_Progress(int percent)
{
	if (percent < 0)
		percent = 0;
	else if (percent > 100)
		percent = 100;

	pthread_mutex_lock(&status_lock);
	// An explicit write is authoritative. ShowProgress() always sets ui_progress
	// to the end of every portion that came before it, so anything the previous
	// portion still had queued up is already accounted for in this number and
	// must not be replayed on top of it.
	slide_frames = 0;
	slide_increment = 0.0f;
	slide_position = (float)percent;
	Set_Progress_Locked(percent);
	pthread_mutex_unlock(&status_lock);
}

// Play out everything the current portion still had left. A new portion always
// supersedes the one before it, exactly like GUIProgressBar::NotifyVarChange().
void TWStatus::Flush_Slide_Locked()
{
	if (slide_frames <= 0)
		return;

	slide_position += slide_increment * (float)slide_frames;
	slide_frames = 0;
	slide_increment = 0.0f;
	Set_Progress_Locked((int)slide_position);
}

void TWStatus::Set_Progress_Portion(float portion)
{
	pthread_mutex_lock(&status_lock);
	Flush_Slide_Locked();
	slide_portion = portion;
	pthread_mutex_unlock(&status_lock);
}

void TWStatus::Set_Progress_Frames(long frames)
{
	pthread_mutex_lock(&status_lock);
	Flush_Slide_Locked();

	// ui_progress_portion is a delta, not a target: the bar climbs by that much
	// on top of where it already is, spread over the frames we are given here.
	if (frames > 0 && slide_portion > 0.0f) {
		slide_frames = frames;
		slide_increment = slide_portion / (float)frames;
		slide_position = (float)current_progress;
		Start_Slide_Thread_Locked();
	}

	slide_portion = 0.0f;
	pthread_mutex_unlock(&status_lock);
}

void* TWStatus::Slide_Thread(void* /* arg */)
{
	// Frames owed but not yet spent, so a coarse tick still plays the slide
	// back at the rate it was authored for.
	float owed = 0.0f;

	for (;;) {
		usleep(TW_SLIDE_INTERVAL_MS * 1000);

		pthread_mutex_lock(&status_lock);
		if (slide_frames > 0) {
			owed += (float)TW_PROGRESS_FPS * TW_SLIDE_INTERVAL_MS / 1000.0f;

			long spend = (long)owed;
			if (spend > slide_frames)
				spend = slide_frames;
			owed -= (float)spend;

			if (spend > 0) {
				slide_position += slide_increment * (float)spend;
				slide_frames -= spend;
				Set_Progress_Locked((int)slide_position);
			}
		} else {
			owed = 0.0f;
		}
		pthread_mutex_unlock(&status_lock);
	}

	return NULL;
}

void TWStatus::Start_Slide_Thread_Locked()
{
#ifndef TW_STATUS_NOTIFY
	// Nobody is listening, so there is nothing to interpolate for.
	return;
#endif
	if (slide_thread_started)
		return;

	pthread_t thread;
	if (pthread_create(&thread, NULL, Slide_Thread, NULL) != 0) {
		LOGERR("Unable to start status progress thread\n");
		return;
	}
	pthread_detach(thread);
	slide_thread_started = true;
}
