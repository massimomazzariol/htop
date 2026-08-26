#ifndef HEADER_HardwareSensor
#define HEADER_HardwareSensor
/*
htop - HardwareSensor.h
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include <stddef.h>
#include <stdint.h>


typedef enum HardwareSensorType_ {
   SENSOR_TEMPERATURE,
   SENSOR_FAN,
} HardwareSensorType;

typedef struct HardwareSensor_ {
   char* id;
   char* chip;
   char* label;
   char* feature;
   HardwareSensorType type;
   double value;
   double min;
   double average;
   double max;
   uint64_t sampleCount;
} HardwareSensor;

void HardwareSensor_formatName(char* buffer, size_t size,
                               const char* chip,
                               const char* label,
                               const char* feature,
                               HardwareSensorType type);

#endif
