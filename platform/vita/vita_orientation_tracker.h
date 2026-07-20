/**************************************************************************/
/*  vita_orientation_tracker.h                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#ifndef VITA_ORIENTATION_TRACKER_H
#define VITA_ORIENTATION_TRACKER_H

#include "core/math/quat.h"
#include "core/math/vector3.h"

class VitaOrientationTracker {
public:
	struct Settings {
		int initialization_sample_count;
		real_t initialization_timeout;
		real_t accelerometer_correction_time;
		real_t absolute_correction_time;
		real_t absolute_max_correction_rate;
		real_t absolute_outlier_angle;
		int absolute_validation_sample_count;
		real_t absolute_validation_tolerance;
		real_t stationary_gyro_threshold;
		real_t acceleration_tolerance;
		real_t gyro_bias_learning_time;

		Settings();
	};

private:
	Settings settings;
	Quat orientation;
	Vector3 gyro_bias;
	Vector3 initialization_acceleration_sum;
	Vector3 initialization_gyro_sum;
	real_t initialization_heading_sin_sum;
	real_t initialization_heading_cos_sum;
	real_t validated_absolute_heading;
	real_t validated_estimated_heading;
	real_t absolute_validation_elapsed;
	real_t elapsed_time;
	uint32_t last_timestamp;
	int initialization_samples;
	int absolute_validation_samples;
	bool timestamp_valid;
	bool tracking;
	bool absolute_reference_acquired;
	bool absolute_validation_anchor_valid;

	static real_t _wrap_angle(real_t p_angle);
	static real_t _time_gain(real_t p_delta, real_t p_time_constant);
	static bool _is_finite(const Vector3 &p_vector);
	static bool _get_heading(const Quat &p_orientation, real_t &r_heading);
	static Quat _orientation_from_up_and_heading(const Vector3 &p_up, real_t p_heading);
	void _reset_initialization_samples();
	void _reset_absolute_validation(real_t p_absolute_heading, real_t p_estimated_heading);
	void _integrate_gyroscope(const Vector3 &p_gyroscope, real_t p_delta);
	void _correct_with_accelerometer(const Vector3 &p_acceleration, real_t p_delta);
	void _correct_with_absolute_heading(real_t p_heading, real_t p_delta);

public:
	void set_settings(const Settings &p_settings);
	void reset();
	void update(const Vector3 &p_acceleration, const Vector3 &p_gyroscope, uint32_t p_timestamp, bool p_absolute_heading_valid, real_t p_absolute_heading);

	Quat get_orientation() const { return orientation; }
	Vector3 get_gyro_bias() const { return gyro_bias; }
	bool is_tracking() const { return tracking; }
	bool is_orientation_available() const { return absolute_reference_acquired; }

	VitaOrientationTracker();
};

#endif // VITA_ORIENTATION_TRACKER_H
