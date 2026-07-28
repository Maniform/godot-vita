/**************************************************************************/
/*  camera_vita.h                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                      https://godotengine.org                           */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#ifndef CAMERA_VITA_H
#define CAMERA_VITA_H

#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/safe_refcount.h"
#include "core/vector.h"
#include "servers/camera/camera_feed.h"
#include "servers/camera_server.h"

#include <psp2/camera.h>
#include <psp2/kernel/sysmem.h>

class CameraVita;

class CameraFeedVita : public CameraFeed {
	GDSOFTCLASS(CameraFeedVita, CameraFeed);

	friend class CameraVita;

	CameraVita *camera_server;
	int device;
	int width;
	int height;
	int resolution;
	int framerate;
	int frame_size;

	SceUID camera_memblock;
	void *camera_buffer;

	Thread capture_thread;
	SafeFlag exit_thread;
	SafeNumeric<int> capture_error;
	Mutex frame_mutex;
	Vector<uint8_t> pending_frame;
	bool frame_pending;

	static void _capture_thread(void *p_userdata);
	void _capture_loop();
	bool _open_camera();
	void _close_camera();
	void _update();

public:
	virtual bool activate_feed();
	virtual void deactivate_feed();

	CameraFeedVita(CameraVita *p_camera_server, int p_device);
	virtual ~CameraFeedVita();
};

class CameraVita : public CameraServer {
	GDSOFTCLASS(CameraVita, CameraServer);

	friend class CameraFeedVita;

	Ref<CameraFeedVita> vita_feeds[2];

	bool _request_activation(CameraFeedVita *p_feed);

public:
	virtual void update();

	CameraVita();
	virtual ~CameraVita();
};

#endif // CAMERA_VITA_H
