#ifndef HEADER_LibSensorMeter
#define HEADER_LibSensorMeter
/*
htop - linux/LibSensorMeter.h
(C) 2026 Massimo Mazzariol
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include <stdbool.h>

#include "Hashtable.h"
#include "Meter.h"
#include "RichString.h"


Hashtable* LibSensorMeter_new(void);

void LibSensorMeter_done(Hashtable* table);

void LibSensorMeter_init(Meter* meter);

void LibSensorMeter_updateValues(Meter* meter);

bool LibSensorMeter_consumeSamplingRequest(void);

void LibSensorMeter_display(const Meter* meter, RichString* out);

#endif
