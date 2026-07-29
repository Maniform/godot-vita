/**************************************************************************/
/*  camera_frame.cpp                                                      */
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

#include "camera_frame.h"

void CameraFrame::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_image"), &CameraFrame::get_image);
	ClassDB::bind_method(D_METHOD("get_frame_id"), &CameraFrame::get_frame_id);
	ClassDB::bind_method(D_METHOD("get_timestamp_usec"), &CameraFrame::get_timestamp_usec);
	ClassDB::bind_method(D_METHOD("get_device_orientation"), &CameraFrame::get_device_orientation);
	ClassDB::bind_method(D_METHOD("get_camera_position"), &CameraFrame::get_camera_position);
	ClassDB::bind_method(D_METHOD("is_orientation_available"), &CameraFrame::is_orientation_available);
	ClassDB::bind_method(D_METHOD("is_orientation_interpolated"), &CameraFrame::is_orientation_interpolated);
	ClassDB::bind_method(D_METHOD("get_orientation_error_usec"), &CameraFrame::get_orientation_error_usec);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "image", PROPERTY_HINT_RESOURCE_TYPE, "Image"), "", "get_image");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "frame_id"), "", "get_frame_id");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "timestamp_usec"), "", "get_timestamp_usec");
	ADD_PROPERTY(PropertyInfo(Variant::QUAT, "device_orientation"), "", "get_device_orientation");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "camera_position", PROPERTY_HINT_ENUM, "Unspecified,Front,Back"), "", "get_camera_position");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "orientation_available"), "", "is_orientation_available");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "orientation_interpolated"), "", "is_orientation_interpolated");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "orientation_error_usec"), "", "get_orientation_error_usec");
}

Ref<Image> CameraFrame::get_image() const {
	return image;
}

uint64_t CameraFrame::get_frame_id() const {
	return frame_id;
}

uint64_t CameraFrame::get_timestamp_usec() const {
	return timestamp_usec;
}

Quat CameraFrame::get_device_orientation() const {
	return device_orientation;
}

CameraFeed::FeedPosition CameraFrame::get_camera_position() const {
	return camera_position;
}

bool CameraFrame::is_orientation_available() const {
	return orientation_available;
}

bool CameraFrame::is_orientation_interpolated() const {
	return orientation_interpolated;
}

uint64_t CameraFrame::get_orientation_error_usec() const {
	return orientation_error_usec;
}

CameraFrame::CameraFrame() :
		frame_id(0),
		timestamp_usec(0),
		device_orientation(),
		camera_position(CameraFeed::FEED_UNSPECIFIED),
		orientation_available(false),
		orientation_interpolated(false),
		orientation_error_usec(0) {
}

CameraFrame::CameraFrame(const Ref<Image> &p_image, uint64_t p_frame_id, uint64_t p_timestamp_usec, const Quat &p_device_orientation, CameraFeed::FeedPosition p_camera_position, bool p_orientation_available, bool p_orientation_interpolated, uint64_t p_orientation_error_usec) :
		image(p_image),
		frame_id(p_frame_id),
		timestamp_usec(p_timestamp_usec),
		device_orientation(p_device_orientation),
		camera_position(p_camera_position),
		orientation_available(p_orientation_available),
		orientation_interpolated(p_orientation_interpolated),
		orientation_error_usec(p_orientation_error_usec) {
}
