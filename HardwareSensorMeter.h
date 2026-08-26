#ifndef HEADER_HardwareSensorMeter
#define HEADER_HardwareSensorMeter
/*
htop - HardwareSensorMeter.h
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include <stdbool.h>

#include "Meter.h"


extern const MeterClass HardwareSensorMeter_class;

bool HardwareSensorMeter_active(void);

#endif
