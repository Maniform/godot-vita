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
#include "core/os/os.h"
#include "core/print_string.h"
#include "core/project_settings.h"
#include "platform/vita/vita_orientation_history.h"
#include "servers/camera/camera_frame.h"

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
		frame_pending(false),
		pending_frame_id(0),
		pending_timestamp_usec(0),
		pending_received_usec(0),
		latest_received_usec(0),
		captured_frames(0),
		published_frames(0),
		dropped_frames(0) {
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

		captured_frames.increment();
		{
			MutexLock lock(frame_mutex);
			if (frame_pending) {
				dropped_frames.increment();
			}
			memcpy(pending_frame.ptrw(), source, frame_size);
			pending_frame_id = camera_read.frame;
			pending_timestamp_usec = camera_read.timestamp;
			pending_received_usec = OS::get_singleton()->get_ticks_usec();
			frame_pending = true;
		}
	}
}

Array CameraFeedVita::get_formats() const {
	static const int widths[] = { 640, 640, 320, 160 };
	static const int heights[] = { 480, 360, 240, 120 };

	Array formats;
	for (int i = 0; i < 4; i++) {
		Dictionary format;
		format["size"] = Size2(widths[i], heights[i]);
		format["fps"] = 30;
		formats.push_back(format);
	}
	return formats;
}

Error CameraFeedVita::set_capture_format(const Size2 &p_size, int p_fps) {
	if (is_active() || capture_thread.is_started()) {
		ERR_PRINT("Vita camera format cannot be changed while the feed is active.");
		return ERR_BUSY;
	}
	if (p_fps != 30 || p_size.x != (int)p_size.x || p_size.y != (int)p_size.y) {
		return ERR_INVALID_PARAMETER;
	}

	const int requested_width = (int)p_size.x;
	const int requested_height = (int)p_size.y;
	int requested_resolution = SCE_CAMERA_RESOLUTION_0_0;
	if (requested_width == 640 && requested_height == 480) {
		requested_resolution = SCE_CAMERA_RESOLUTION_640_480;
	} else if (requested_width == 640 && requested_height == 360) {
		requested_resolution = SCE_CAMERA_RESOLUTION_640_360;
	} else if (requested_width == 320 && requested_height == 240) {
		requested_resolution = SCE_CAMERA_RESOLUTION_320_240;
	} else if (requested_width == 160 && requested_height == 120) {
		requested_resolution = SCE_CAMERA_RESOLUTION_160_120;
	} else {
		return ERR_INVALID_PARAMETER;
	}

	width = requested_width;
	height = requested_height;
	resolution = requested_resolution;
	framerate = SCE_CAMERA_FRAMERATE_30_FPS;
	frame_size = width * height * 4;
	pending_frame.resize(frame_size);
	latest_frame.unref();
	latest_luminance_frame.unref();
	latest_received_usec = 0;
	return OK;
}

Ref<CameraFrame> CameraFeedVita::get_latest_frame(FrameFormat p_format) const {
	if (!is_active()) {
		return Ref<CameraFrame>();
	}

	Ref<CameraFrame> source_frame;
	if (p_format == FRAME_RGBA) {
		source_frame = latest_frame;
	} else if (p_format == FRAME_LUMINANCE) {
		source_frame = latest_luminance_frame;
	} else {
		return Ref<CameraFrame>();
	}

	if (source_frame.is_null() || source_frame->get_image().is_null()) {
		return Ref<CameraFrame>();
	}

	Ref<Image> image = source_frame->get_image()->duplicate();
	return Ref<CameraFrame>(memnew(CameraFrame(image, source_frame->get_frame_id(), source_frame->get_timestamp_usec(), source_frame->get_device_orientation(), source_frame->get_camera_position(), source_frame->is_orientation_available(), source_frame->is_orientation_interpolated(), source_frame->get_orientation_error_usec())));
}

Dictionary CameraFeedVita::get_calibration() const {
	const String prefix = device == SCE_CAMERA_DEVICE_FRONT ? "camera/vita/calibration/front/" : "camera/vita/calibration/rear/";
	const Size2 reference_size = GLOBAL_GET(prefix + "image_size");
	const Vector2 reference_focal_length = GLOBAL_GET(prefix + "focal_length");
	const Vector2 reference_principal_point = GLOBAL_GET(prefix + "principal_point");

	Dictionary calibration;
	calibration["valid"] = reference_size.x > 0.0f && reference_size.y > 0.0f && reference_focal_length.x > 0.0f && reference_focal_length.y > 0.0f;
	calibration["reference_size"] = reference_size;
	calibration["size"] = Size2(width, height);

	if (reference_size.x > 0.0f && reference_size.y > 0.0f) {
		const real_t scale = MAX((real_t)width / reference_size.x, (real_t)height / reference_size.y);
		const Vector2 crop_offset((reference_size.x * scale - width) * 0.5f, (reference_size.y * scale - height) * 0.5f);
		calibration["focal_length"] = reference_focal_length * scale;
		calibration["principal_point"] = reference_principal_point * scale - crop_offset;
	} else {
		calibration["focal_length"] = reference_focal_length;
		calibration["principal_point"] = reference_principal_point;
	}

	calibration["radial_distortion"] = GLOBAL_GET(prefix + "radial_distortion");
	calibration["tangential_distortion"] = GLOBAL_GET(prefix + "tangential_distortion");
	calibration["camera_to_device_rotation"] = GLOBAL_GET(prefix + "camera_to_device_rotation");
	calibration["camera_to_device_translation"] = GLOBAL_GET(prefix + "camera_to_device_translation");
	return calibration;
}

Dictionary CameraFeedVita::get_diagnostics() const {
	Dictionary diagnostics;
	diagnostics["active"] = is_active();
	diagnostics["captured_frames"] = captured_frames.get();
	diagnostics["published_frames"] = published_frames.get();
	diagnostics["dropped_frames"] = dropped_frames.get();
	diagnostics["latest_frame_age_usec"] = latest_received_usec == 0 ? 0 : OS::get_singleton()->get_ticks_usec() - latest_received_usec;
	diagnostics["latest_frame_id"] = latest_frame.is_valid() ? latest_frame->get_frame_id() : 0;
	diagnostics["latest_timestamp_usec"] = latest_frame.is_valid() ? latest_frame->get_timestamp_usec() : 0;
	diagnostics["size"] = Size2(width, height);
	diagnostics["fps"] = 30;
	return diagnostics;
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
	captured_frames.set(0);
	published_frames.set(0);
	dropped_frames.set(0);
	latest_received_usec = 0;
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
	latest_frame.unref();
	latest_luminance_frame.unref();
	latest_received_usec = 0;
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
	uint64_t frame_id = 0;
	uint64_t timestamp_usec = 0;
	uint64_t received_usec = 0;
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
		frame_id = pending_frame_id;
		timestamp_usec = pending_timestamp_usec;
		received_usec = pending_received_usec;
		frame_pending = false;
	}

	PoolVector<uint8_t> luminance_data;
	luminance_data.resize(width * height);
	{
		PoolVector<uint8_t>::Read rgba = image_data.read();
		PoolVector<uint8_t>::Write luminance = luminance_data.write();
		for (int i = 0; i < width * height; i++) {
			const int rgba_offset = i * 4;
			luminance[i] = (77 * rgba[rgba_offset] + 150 * rgba[rgba_offset + 1] + 29 * rgba[rgba_offset + 2] + 128) >> 8;
		}
	}

	Quat orientation;
	bool orientation_interpolated = false;
	uint64_t orientation_error_usec = 0;
	const bool orientation_available = VitaOrientationHistory::get_singleton()->get_orientation(timestamp_usec, orientation, orientation_interpolated, orientation_error_usec);

	Ref<Image> image;
	image.instance();
	image->create(width, height, false, Image::FORMAT_RGBA8, image_data);
	Ref<Image> luminance_image;
	luminance_image.instance();
	luminance_image->create(width, height, false, Image::FORMAT_L8, luminance_data);

	latest_frame = Ref<CameraFrame>(memnew(CameraFrame(image, frame_id, timestamp_usec, orientation, get_position(), orientation_available, orientation_interpolated, orientation_error_usec)));
	latest_luminance_frame = Ref<CameraFrame>(memnew(CameraFrame(luminance_image, frame_id, timestamp_usec, orientation, get_position(), orientation_available, orientation_interpolated, orientation_error_usec)));
	latest_received_usec = received_usec;
	published_frames.increment();
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
