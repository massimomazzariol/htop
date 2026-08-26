#ifndef HEADER_HardwareSensorDynamicMeter
#define HEADER_HardwareSensorDynamicMeter
/*
htop - linux/HardwareSensorDynamicMeter.h
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "Hashtable.h"
#include "Meter.h"
#include "RichString.h"


Hashtable* HardwareSensorDynamicMeters_new(void);

void HardwareSensorDynamicMeters_done(Hashtable* table);

void HardwareSensorDynamicMeter_init(Meter* meter);

void HardwareSensorDynamicMeter_updateValues(Meter* meter);

bool HardwareSensorDynamicMeter_consumeSamplingRequest(void);

void HardwareSensorDynamicMeter_display(const Meter* meter, RichString* out);

#endif
