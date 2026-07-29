/**************************************************************************/
/*  camera_frame.h                                                        */
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

#ifndef CAMERA_FRAME_H
#define CAMERA_FRAME_H

#include "core/image.h"
#include "core/math/quat.h"
#include "core/reference.h"
#include "servers/camera/camera_feed.h"

class CameraFrame : public Reference {
	GDCLASS(CameraFrame, Reference);

	Ref<Image> image;
	uint64_t frame_id;
	uint64_t timestamp_usec;
	Quat device_orientation;
	CameraFeed::FeedPosition camera_position;
	bool orientation_available;

protected:
	static void _bind_methods();

public:
	Ref<Image> get_image() const;
	uint64_t get_frame_id() const;
	uint64_t get_timestamp_usec() const;
	Quat get_device_orientation() const;
	CameraFeed::FeedPosition get_camera_position() const;
	bool is_orientation_available() const;

	CameraFrame();
	CameraFrame(const Ref<Image> &p_image, uint64_t p_frame_id, uint64_t p_timestamp_usec, const Quat &p_device_orientation, CameraFeed::FeedPosition p_camera_position, bool p_orientation_available);
};

#endif // CAMERA_FRAME_H
