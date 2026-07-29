/**************************************************************************/
/*  vita_orientation_history.h                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#ifndef VITA_ORIENTATION_HISTORY_H
#define VITA_ORIENTATION_HISTORY_H

#include "core/math/quat.h"
#include "core/os/mutex.h"

class VitaOrientationHistory {
	struct TimestampedOrientation {
		uint64_t timestamp_usec;
		Quat orientation;
		bool available;
	};

	static const int HISTORY_SIZE = 128;

	mutable Mutex mutex;
	TimestampedOrientation history[HISTORY_SIZE];
	int history_start;
	int history_count;

	VitaOrientationHistory();

public:
	static VitaOrientationHistory *get_singleton();

	void clear();
	void add_sample(uint64_t p_timestamp_usec, const Quat &p_orientation, bool p_available);
	bool get_orientation(uint64_t p_timestamp_usec, Quat &r_orientation, bool &r_interpolated, uint64_t &r_error_usec) const;
};

#endif // VITA_ORIENTATION_HISTORY_H
