/**************************************************************************/
/*  vita_orientation_tracker.cpp                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#include "vita_orientation_tracker.h"

#include "core/math/math_funcs.h"

VitaOrientationTracker::Settings::Settings() {
	initialization_sample_count = 1;
	initialization_timeout = 1.0f;
	accelerometer_correction_time = 0.5f;
	absolute_correction_time = 4.0f;
	absolute_max_correction_rate = Math::deg2rad(3.0f);
	absolute_outlier_angle = Math::deg2rad(90.0f);
	absolute_validation_sample_count = 15;
	absolute_validation_tolerance = Math::deg2rad(8.0f);
	stationary_gyro_threshold = 0.015f;
	acceleration_tolerance = 0.2f;
	gyro_bias_learning_time = 10.0f;
}

real_t VitaOrientationTracker::_wrap_angle(real_t p_angle) {
	return Math::fposmod(p_angle + (real_t)Math_PI, (real_t)Math_TAU) - (real_t)Math_PI;
}

real_t VitaOrientationTracker::_time_gain(real_t p_delta, real_t p_time_constant) {
	if (p_delta <= 0.0f || p_time_constant <= 0.0f) {
		return 0.0f;
	}
	return 1.0f - Math::exp(-p_delta / p_time_constant);
}

bool VitaOrientationTracker::_is_finite(const Vector3 &p_vector) {
	return !Math::is_nan(p_vector.x) && !Math::is_nan(p_vector.y) && !Math::is_nan(p_vector.z) &&
			!Math::is_inf(p_vector.x) && !Math::is_inf(p_vector.y) && !Math::is_inf(p_vector.z);
}

bool VitaOrientationTracker::_get_heading(const Quat &p_orientation, real_t &r_heading) {
	Vector3 forward = p_orientation.xform(Vector3(0.0f, 0.0f, -1.0f));
	forward.y = 0.0f;
	if (forward.length_squared() <= CMP_EPSILON) {
		return false;
	}

	forward.normalize();
	r_heading = Math::atan2(-forward.x, -forward.z);
	return true;
}

Quat VitaOrientationTracker::_orientation_from_up_and_heading(const Vector3 &p_up, real_t p_heading) {
	const Vector3 measured_up = -p_up.normalized();
	Quat result(measured_up, Vector3(0.0f, 1.0f, 0.0f));
	result.normalize();

	real_t current_heading = 0.0f;
	if (_get_heading(result, current_heading)) {
		result = Quat(Vector3(0.0f, 1.0f, 0.0f), _wrap_angle(p_heading - current_heading)) * result;
	}
	return result.normalized();
}

void VitaOrientationTracker::_reset_initialization_samples() {
	initialization_acceleration_sum = Vector3();
	initialization_gyro_sum = Vector3();
	initialization_heading_sin_sum = 0.0f;
	initialization_heading_cos_sum = 0.0f;
	initialization_samples = 0;
}

void VitaOrientationTracker::_reset_absolute_validation(real_t p_absolute_heading, real_t p_estimated_heading) {
	validated_absolute_heading = p_absolute_heading;
	validated_estimated_heading = p_estimated_heading;
	absolute_validation_samples = 0;
	absolute_validation_elapsed = 0.0f;
	absolute_validation_anchor_valid = true;
}

void VitaOrientationTracker::_integrate_gyroscope(const Vector3 &p_gyroscope, real_t p_delta) {
	const Vector3 angular_velocity = p_gyroscope - gyro_bias;
	const real_t angular_speed = angular_velocity.length();
	if (angular_speed <= CMP_EPSILON || p_delta <= 0.0f) {
		return;
	}

	const Quat delta_orientation(angular_velocity / angular_speed, angular_speed * p_delta);
	orientation = (orientation * delta_orientation).normalized();
}

void VitaOrientationTracker::_correct_with_accelerometer(const Vector3 &p_acceleration, real_t p_delta) {
	const real_t acceleration_length = p_acceleration.length();
	if (acceleration_length <= CMP_EPSILON || Math::absf(acceleration_length - 1.0f) > settings.acceleration_tolerance) {
		return;
	}

	const Vector3 measured_up_world = orientation.xform(-p_acceleration / acceleration_length);
	const Quat full_correction(measured_up_world, Vector3(0.0f, 1.0f, 0.0f));
	const real_t gain = _time_gain(p_delta, settings.accelerometer_correction_time);
	if (gain > 0.0f) {
		const Quat correction = Quat().slerp(full_correction.normalized(), gain).normalized();
		orientation = (correction * orientation).normalized();
	}
}

void VitaOrientationTracker::_correct_with_absolute_heading(real_t p_heading, real_t p_delta) {
	real_t current_heading = 0.0f;
	if (!_get_heading(orientation, current_heading)) {
		return;
	}

	const real_t error = _wrap_angle(p_heading - current_heading);
	if (Math::absf(error) > settings.absolute_outlier_angle) {
		return;
	}

	real_t correction = error * _time_gain(p_delta, settings.absolute_correction_time);
	const real_t maximum_step = settings.absolute_max_correction_rate * p_delta;
	correction = CLAMP(correction, -maximum_step, maximum_step);
	if (Math::absf(correction) > CMP_EPSILON) {
		orientation = (Quat(Vector3(0.0f, 1.0f, 0.0f), correction) * orientation).normalized();
	}
}

void VitaOrientationTracker::set_settings(const Settings &p_settings) {
	settings = p_settings;
	settings.initialization_sample_count = MAX(1, settings.initialization_sample_count);
	settings.initialization_timeout = MAX(0.0f, settings.initialization_timeout);
	settings.accelerometer_correction_time = MAX(0.0f, settings.accelerometer_correction_time);
	settings.absolute_correction_time = MAX(0.0f, settings.absolute_correction_time);
	settings.absolute_max_correction_rate = MAX(0.0f, settings.absolute_max_correction_rate);
	settings.absolute_outlier_angle = CLAMP(settings.absolute_outlier_angle, 0.0f, (real_t)Math_PI);
	settings.absolute_validation_sample_count = MAX(1, settings.absolute_validation_sample_count);
	settings.absolute_validation_tolerance = CLAMP(settings.absolute_validation_tolerance, 0.0f, (real_t)Math_PI);
	settings.stationary_gyro_threshold = MAX(0.0f, settings.stationary_gyro_threshold);
	settings.acceleration_tolerance = MAX(0.0f, settings.acceleration_tolerance);
	settings.gyro_bias_learning_time = MAX(0.0f, settings.gyro_bias_learning_time);
}

void VitaOrientationTracker::reset() {
	orientation = Quat();
	gyro_bias = Vector3();
	elapsed_time = 0.0f;
	last_timestamp = 0;
	timestamp_valid = false;
	tracking = false;
	absolute_reference_acquired = false;
	absolute_validation_anchor_valid = false;
	absolute_validation_samples = 0;
	absolute_validation_elapsed = 0.0f;
	validated_absolute_heading = 0.0f;
	validated_estimated_heading = 0.0f;
	_reset_initialization_samples();
}

void VitaOrientationTracker::update(const Vector3 &p_acceleration, const Vector3 &p_gyroscope, uint32_t p_timestamp, bool p_absolute_heading_valid, real_t p_absolute_heading) {
	if (!_is_finite(p_acceleration) || !_is_finite(p_gyroscope) || (p_absolute_heading_valid && (Math::is_nan(p_absolute_heading) || Math::is_inf(p_absolute_heading)))) {
		return;
	}

	if (timestamp_valid && p_timestamp == last_timestamp) {
		return;
	}

	real_t delta = 0.0f;
	if (timestamp_valid) {
		const uint32_t timestamp_delta = p_timestamp - last_timestamp;
		delta = CLAMP((real_t)timestamp_delta / 1000000.0f, 0.0f, 0.1f);
	}
	last_timestamp = p_timestamp;
	timestamp_valid = true;
	elapsed_time += delta;

	const real_t acceleration_length = p_acceleration.length();
	const bool acceleration_reliable = acceleration_length > CMP_EPSILON && Math::absf(acceleration_length - 1.0f) <= settings.acceleration_tolerance;
	const bool stationary = acceleration_reliable && (p_gyroscope - gyro_bias).length() <= settings.stationary_gyro_threshold;

	if (tracking) {
		_integrate_gyroscope(p_gyroscope, delta);
		_correct_with_accelerometer(p_acceleration, delta);

		if (stationary) {
			const real_t bias_gain = _time_gain(delta, settings.gyro_bias_learning_time);
			gyro_bias = gyro_bias.linear_interpolate(p_gyroscope, bias_gain);
		}
	}

	if (!absolute_reference_acquired) {
		if (p_absolute_heading_valid && stationary) {
			initialization_acceleration_sum += p_acceleration;
			initialization_gyro_sum += p_gyroscope;
			initialization_heading_sin_sum += Math::sin(p_absolute_heading);
			initialization_heading_cos_sum += Math::cos(p_absolute_heading);
			initialization_samples++;
		} else {
			_reset_initialization_samples();
		}
	}

	if (!absolute_reference_acquired && initialization_samples >= settings.initialization_sample_count) {
		const real_t initial_heading = Math::atan2(initialization_heading_sin_sum, initialization_heading_cos_sum);
		gyro_bias = initialization_gyro_sum / (real_t)initialization_samples;
		orientation = _orientation_from_up_and_heading(initialization_acceleration_sum, initial_heading);
		tracking = true;
		absolute_reference_acquired = true;
		real_t initial_estimated_heading = initial_heading;
		_get_heading(orientation, initial_estimated_heading);
		_reset_absolute_validation(initial_heading, initial_estimated_heading);
		_reset_initialization_samples();
		return;
	}

	if (!tracking && elapsed_time >= settings.initialization_timeout && acceleration_reliable) {
		// Track tilt and relative motion while waiting for a trustworthy north
		// reference, but do not expose this arbitrary-yaw orientation to scripts.
		orientation = _orientation_from_up_and_heading(p_acceleration, 0.0f);
		tracking = true;
	}

	if (!tracking || !absolute_reference_acquired) {
		return;
	}

	if (!p_absolute_heading_valid || !stationary) {
		absolute_validation_samples = 0;
		absolute_validation_elapsed = 0.0f;
		return;
	}

	real_t estimated_heading = 0.0f;
	if (!_get_heading(orientation, estimated_heading)) {
		absolute_validation_samples = 0;
		absolute_validation_elapsed = 0.0f;
		return;
	}

	if (!absolute_validation_anchor_valid) {
		_reset_absolute_validation(p_absolute_heading, estimated_heading);
		return;
	}

	const real_t absolute_delta = _wrap_angle(p_absolute_heading - validated_absolute_heading);
	const real_t estimated_delta = _wrap_angle(estimated_heading - validated_estimated_heading);
	const real_t inertial_disagreement = _wrap_angle(absolute_delta - estimated_delta);
	if (Math::absf(inertial_disagreement) > settings.absolute_validation_tolerance) {
		absolute_validation_samples = 0;
		absolute_validation_elapsed = 0.0f;
		return;
	}

	absolute_validation_samples++;
	absolute_validation_elapsed += delta;
	if (absolute_validation_samples >= settings.absolute_validation_sample_count) {
		_correct_with_absolute_heading(p_absolute_heading, absolute_validation_elapsed);
		_get_heading(orientation, estimated_heading);
		_reset_absolute_validation(p_absolute_heading, estimated_heading);
	}
}

VitaOrientationTracker::VitaOrientationTracker() {
	reset();
}
