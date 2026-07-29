/**************************************************************************/
/*  vita_ar_marker_tracker.h                                              */
/**************************************************************************/

#ifndef VITA_AR_MARKER_TRACKER_H
#define VITA_AR_MARKER_TRACKER_H

#include "core/image.h"
#include "core/reference.h"

class VitaARMarkerTracker : public Reference {
	GDCLASS(VitaARMarkerTracker, Reference);

	static void _bind_methods();

public:
	Dictionary track(const Ref<Image> &p_image, const Dictionary &p_calibration, real_t p_marker_size = 0.08) const;
};

#endif // VITA_AR_MARKER_TRACKER_H
