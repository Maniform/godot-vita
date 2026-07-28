/**************************************************************************/
/*  camera_vita.cpp                                                       */
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

#include "camera_vita.h"

#include "core/image.h"
#include "core/print_string.h"

#include <string.h>

static const int VITA_CAMERA_BUFFER_ALIGNMENT = 256 * 1024;

static String _camera_error(const char *p_operation, int p_device, int p_error) {
	return vformat("Vita camera %s failed for device %d (error 0x%08x).", p_operation, p_device, (uint32_t)p_error);
}

CameraFeedVita::CameraFeedVita(CameraVita *p_camera_server, int p_device) :
		CameraFeed(p_device == SCE_CAMERA_DEVICE_FRONT ? "PS Vita Front Camera" : "PS Vita Rear Camera",
				p_device == SCE_CAMERA_DEVICE_FRONT ? CameraFeed::FEED_FRONT : CameraFeed::FEED_BACK),
		camera_server(p_camera_server),
		device(p_device),
		width(640),
		height(480),
		resolution(SCE_CAMERA_RESOLUTION_640_480),
		framerate(SCE_CAMERA_FRAMERATE_30_FPS),
		frame_size(width * height * 4),
		camera_memblock(-1),
		camera_buffer(nullptr),
		exit_thread(false),
		capture_error(0),
		frame_pending(false) {
	pending_frame.resize(frame_size);

	if (device == SCE_CAMERA_DEVICE_FRONT) {
		// Mirror the front camera when it is used as an Environment background.
		set_transform(Transform2D(-1.0, 0.0, 0.0, -1.0, 1.0, 1.0));
	}
}

CameraFeedVita::~CameraFeedVita() {
	deactivate_feed();
}

bool CameraFeedVita::_open_camera() {
	const int allocation_size = (frame_size + VITA_CAMERA_BUFFER_ALIGNMENT - 1) & ~(VITA_CAMERA_BUFFER_ALIGNMENT - 1);
	camera_memblock = sceKernelAllocMemBlock("godot_camera", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, allocation_size, nullptr);
	if (camera_memblock < 0) {
		ERR_PRINT(_camera_error("memory allocation", device, camera_memblock));
		camera_memblock = -1;
		return false;
	}

	int result = sceKernelGetMemBlockBase(camera_memblock, &camera_buffer);
	if (result < 0 || camera_buffer == nullptr) {
		ERR_PRINT(_camera_error("memory mapping", device, result));
		_close_camera();
		return false;
	}

	SceCameraInfo camera_info = {};
	camera_info.size = sizeof(SceCameraInfo);
	camera_info.priority = SCE_CAMERA_PRIORITY_SHARE;
	camera_info.format = SCE_CAMERA_FORMAT_ABGR;
	camera_info.resolution = resolution;
	camera_info.framerate = framerate;
	camera_info.sizeIBase = frame_size;
	camera_info.pIBase = camera_buffer;
	camera_info.pitch = 0;
	camera_info.buffer = 0;

	result = sceCameraOpen(device, &camera_info);
	if (result < 0) {
		ERR_PRINT(_camera_error("open", device, result));
		_close_camera();
		return false;
	}

	result = sceCameraStart(device);
	if (result < 0) {
		ERR_PRINT(_camera_error("start", device, result));
		sceCameraClose(device);
		_close_camera();
		return false;
	}

	return true;
}

void CameraFeedVita::_close_camera() {
	if (camera_memblock >= 0) {
		sceKernelFreeMemBlock(camera_memblock);
		camera_memblock = -1;
	}
	camera_buffer = nullptr;
}

void CameraFeedVita::_capture_thread(void *p_userdata) {
	CameraFeedVita *feed = static_cast<CameraFeedVita *>(p_userdata);
	Thread::set_name(feed->device == SCE_CAMERA_DEVICE_FRONT ? "VitaCameraFront" : "VitaCameraRear");
	feed->_capture_loop();
}

void CameraFeedVita::_capture_loop() {
	while (!exit_thread.is_set()) {
		SceCameraRead camera_read = {};
		camera_read.size = sizeof(SceCameraRead);
		camera_read.mode = 0; // Blocking read.

		const int result = sceCameraRead(device, &camera_read);
		if (result < 0) {
			if (!exit_thread.is_set()) {
				capture_error.set(result);
			}
			break;
		}

		const void *source = camera_read.pIBase != nullptr ? camera_read.pIBase : camera_buffer;
		if (source == nullptr || (camera_read.sizeIBase > 0 && camera_read.sizeIBase < (SceSize)frame_size)) {
			capture_error.set(SCE_CAMERA_ERROR_FATAL);
			break;
		}

		{
			MutexLock lock(frame_mutex);
			memcpy(pending_frame.ptrw(), source, frame_size);
			frame_pending = true;
		}
	}
}

bool CameraFeedVita::activate_feed() {
	if (capture_thread.is_started()) {
		return true;
	}

	if (camera_server == nullptr || !camera_server->_request_activation(this)) {
		return false;
	}

	exit_thread.clear();
	capture_error.set(0);
	{
		MutexLock lock(frame_mutex);
		frame_pending = false;
	}

	if (!_open_camera()) {
		return false;
	}

	capture_thread.start(_capture_thread, this);
	if (!capture_thread.is_started()) {
		ERR_PRINT("Vita camera could not start its capture thread.");
		sceCameraStop(device);
		sceCameraClose(device);
		_close_camera();
		return false;
	}

	return true;
}

void CameraFeedVita::deactivate_feed() {
	exit_thread.set();
	if (capture_thread.is_started()) {
		capture_thread.wait_to_finish();
	}

	if (camera_buffer != nullptr) {
		const int stop_result = sceCameraStop(device);
		if (stop_result < 0 && stop_result != (int)SCE_CAMERA_ERROR_NOT_ACTIVE) {
			ERR_PRINT(_camera_error("stop", device, stop_result));
		}
		const int close_result = sceCameraClose(device);
		if (close_result < 0 && close_result != (int)SCE_CAMERA_ERROR_NOT_OPEN) {
			ERR_PRINT(_camera_error("close", device, close_result));
		}
	}

	_close_camera();
	capture_error.set(0);
	{
		MutexLock lock(frame_mutex);
		frame_pending = false;
	}
}

void CameraFeedVita::_update() {
	const int error = capture_error.get();
	if (error < 0) {
		ERR_PRINT(_camera_error("read", device, error));
		set_active(false);
		return;
	}

	PoolVector<uint8_t> image_data;
	{
		MutexLock lock(frame_mutex);
		if (!frame_pending) {
			return;
		}
		image_data.resize(frame_size);
		PoolVector<uint8_t>::Write write = image_data.write();

		// SCE_CAMERA_FORMAT_ABGR is A8B8G8R8 as a 32-bit value. On the
		// little-endian Vita its memory byte order is R, G, B, A, matching
		// Image::FORMAT_RGBA8.
		memcpy(write.ptr(), pending_frame.ptr(), frame_size);
		frame_pending = false;
	}

	Ref<Image> image;
	image.instance();
	image->create(width, height, false, Image::FORMAT_RGBA8, image_data);
	set_RGB_img(image);
}

bool CameraVita::_request_activation(CameraFeedVita *p_feed) {
	for (int i = 0; i < 2; i++) {
		if (vita_feeds[i].is_valid() && vita_feeds[i].ptr() != p_feed && vita_feeds[i]->is_active()) {
			vita_feeds[i]->set_active(false);
		}
	}
	return true;
}

void CameraVita::update() {
	for (int i = 0; i < 2; i++) {
		if (vita_feeds[i].is_valid() && vita_feeds[i]->is_active()) {
			vita_feeds[i]->_update();
		}
	}
}

CameraVita::CameraVita() {
	vita_feeds[0] = Ref<CameraFeedVita>(memnew(CameraFeedVita(this, SCE_CAMERA_DEVICE_FRONT)));
	add_feed(vita_feeds[0]);

	vita_feeds[1] = Ref<CameraFeedVita>(memnew(CameraFeedVita(this, SCE_CAMERA_DEVICE_BACK)));
	add_feed(vita_feeds[1]);
}

CameraVita::~CameraVita() {
	for (int i = 0; i < 2; i++) {
		if (vita_feeds[i].is_valid() && vita_feeds[i]->is_active()) {
			vita_feeds[i]->set_active(false);
		}
	}
}
