/**************************************************************************/
/*  vita_orientation_history.cpp                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#include "vita_orientation_history.h"

static const uint64_t VITA_ORIENTATION_MAX_ERROR_USEC = 100000;

VitaOrientationHistory::VitaOrientationHistory() :
		history_start(0),
		history_count(0) {
}

VitaOrientationHistory *VitaOrientationHistory::get_singleton() {
	static VitaOrientationHistory singleton;
	return &singleton;
}

void VitaOrientationHistory::clear() {
	MutexLock lock(mutex);
	history_start = 0;
	history_count = 0;
}

void VitaOrientationHistory::add_sample(uint64_t p_timestamp_usec, const Quat &p_orientation, bool p_available) {
	if (p_timestamp_usec == 0) {
		return;
	}

	MutexLock lock(mutex);
	int index = (history_start + history_count) % HISTORY_SIZE;
	if (history_count == HISTORY_SIZE) {
		index = history_start;
		history_start = (history_start + 1) % HISTORY_SIZE;
	} else {
		history_count++;
	}

	history[index].timestamp_usec = p_timestamp_usec;
	history[index].orientation = p_orientation;
	history[index].available = p_available;
}

bool VitaOrientationHistory::get_orientation(uint64_t p_timestamp_usec, Quat &r_orientation, bool &r_interpolated, uint64_t &r_error_usec) const {
	r_orientation = Quat();
	r_interpolated = false;
	r_error_usec = 0;

	MutexLock lock(mutex);
	const TimestampedOrientation *before = nullptr;
	const TimestampedOrientation *after = nullptr;

	for (int i = 0; i < history_count; i++) {
		const TimestampedOrientation &sample = history[(history_start + i) % HISTORY_SIZE];
		if (!sample.available) {
			continue;
		}
		if (sample.timestamp_usec <= p_timestamp_usec) {
			before = &sample;
		}
		if (sample.timestamp_usec >= p_timestamp_usec) {
			after = &sample;
			break;
		}
	}

	const uint64_t before_error = before == nullptr ? UINT64_MAX : p_timestamp_usec - before->timestamp_usec;
	const uint64_t after_error = after == nullptr ? UINT64_MAX : after->timestamp_usec - p_timestamp_usec;
	if (before != nullptr && after != nullptr && before_error <= VITA_ORIENTATION_MAX_ERROR_USEC && after_error <= VITA_ORIENTATION_MAX_ERROR_USEC) {
		const uint64_t interval = after->timestamp_usec - before->timestamp_usec;
		if (interval == 0) {
			r_orientation = before->orientation;
		} else {
			const real_t weight = (real_t)before_error / (real_t)interval;
			r_orientation = before->orientation.slerp(after->orientation, weight).normalized();
		}
		r_interpolated = true;
		return true;
	}

	const TimestampedOrientation *nearest = before_error <= after_error ? before : after;
	const uint64_t nearest_error = MIN(before_error, after_error);
	if (nearest == nullptr || nearest_error > VITA_ORIENTATION_MAX_ERROR_USEC) {
		return false;
	}

	r_orientation = nearest->orientation;
	r_error_usec = nearest_error;
	return true;
}
