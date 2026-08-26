/*
htop - linux/HardwareSensorDynamicMeter.c
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h" // IWYU pragma: keep

#include "linux/HardwareSensorDynamicMeter.h"

#ifdef HAVE_SENSORS_SENSORS_H

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "CRT.h"
#include "DynamicMeter.h"
#include "HardwareSensor.h"
#include "Macros.h"
#include "Settings.h"
#include "XUtils.h"
#include "linux/LibSensors.h"
#include "linux/LinuxMachine.h"


typedef struct HardwareSensorDynamicMeter_ {
   DynamicMeter super;
   char* sensorId;
   size_t displayNameWidth;
   size_t maxDisplayNameWidth;
} HardwareSensorDynamicMeter;


static bool hardwareSensorSamplingRequested = false;


static uint64_t HardwareSensorDynamicMeter_hash64(const char* value) {
   uint64_t hash = UINT64_C(14695981039346656037);

   for (const unsigned char* p = (const unsigned char*)value; *p; p++) {
      hash ^= *p;
      hash *= UINT64_C(1099511628211);
   }

   return hash;
}

static uint32_t HardwareSensorDynamicMeter_hash32(const char* value) {
   uint32_t hash = UINT32_C(2166136261);

   for (const unsigned char* p = (const unsigned char*)value; *p; p++) {
      hash ^= *p;
      hash *= UINT32_C(16777619);
   }

   return hash;
}

static void HardwareSensorDynamicMeter_makeStableName(char name[32], const char* sensorId) {
   uint64_t hash64 = HardwareSensorDynamicMeter_hash64(sensorId);
   uint32_t hash32 = HardwareSensorDynamicMeter_hash32(sensorId);

   /*
    * Header parsing currently accepts at most 30 characters inside
    * Dynamic(...). Keep the persisted sensor name below that limit while the
    * complete canonical sensor ID remains stored separately in sensorId.
    */
   xSnprintf(name, 32, "Sensor_%016" PRIx64 "%06" PRIx32,
             hash64, hash32 >> 8);
}

static void HardwareSensorDynamicMeter_formatFriendlyName(
   const HardwareSensor* sensors,
   size_t sensorCount,
   size_t index,
   char* buffer,
   size_t size) {

   const HardwareSensor* sensor = &sensors[index];

   char base[128];
   HardwareSensor_formatName(base, sizeof(base),
                             sensor->chip,
                             sensor->label,
                             sensor->feature,
                             sensor->type);

   bool duplicate = false;

   for (size_t i = 0; i < sensorCount; i++) {
      if (i == index)
         continue;

      char other[128];
      HardwareSensor_formatName(other, sizeof(other),
                                sensors[i].chip,
                                sensors[i].label,
                                sensors[i].feature,
                                sensors[i].type);

      if (String_eq(base, other)) {
         duplicate = true;
         break;
      }
   }

   if (duplicate && sensor->id)
      xSnprintf(buffer, size, "%s (%s)", base, sensor->id);
   else
      xSnprintf(buffer, size, "%s", base);
}

static const HardwareSensorDynamicMeter* HardwareSensorDynamicMeter_lookupDefinition(
   const Meter* meter) {

   if (!meter->host || !meter->host->settings)
      return NULL;

   Hashtable* dynamicMeters = meter->host->settings->dynamicMeters;
   if (!dynamicMeters)
      return NULL;

   return (const HardwareSensorDynamicMeter*)Hashtable_get(dynamicMeters, meter->param);
}

static const HardwareSensor* HardwareSensorDynamicMeter_lookupSensor(
   const Meter* meter,
   const HardwareSensorDynamicMeter* definition) {

   const LinuxMachine* host = (const LinuxMachine*)meter->host;

   if (!definition || !definition->sensorId)
      return NULL;

   for (size_t i = 0; i < host->sensorCount; i++) {
      const HardwareSensor* sensor = &host->sensors[i];

      if (sensor->id && String_eq(sensor->id, definition->sensorId))
         return sensor;
   }

   return NULL;
}

static size_t HardwareSensorDynamicMeter_formatValue(
   char* buffer,
   size_t size,
   HardwareSensorType type,
   double value,
   char temperatureUnit) {

   char number[32];
   xSnprintf(number, sizeof(number), "%.0f", value);

   switch (type) {
      case SENSOR_TEMPERATURE:
         xSnprintf(buffer, size, "%s%s%c", number, CRT_degreeSign, temperatureUnit);
         return strlen(number) + 2;

      case SENSOR_FAN:
         xSnprintf(buffer, size, "%s RPM", number);
         return strlen(buffer);

      default:
         xSnprintf(buffer, size, "%s", number);
         return strlen(buffer);
   }
}

static void HardwareSensorDynamicMeter_convertTemperature(
   const Settings* settings,
   HardwareSensorType type,
   double* current,
   double* min,
   double* average,
   double* max) {

#ifdef BUILD_WITH_CPU_TEMP
   if (type == SENSOR_TEMPERATURE && settings->degreeFahrenheit) {
      *current = *current * 9 / 5 + 32;
      *min = *min * 9 / 5 + 32;
      *average = *average * 9 / 5 + 32;
      *max = *max * 9 / 5 + 32;
   }
#else
   (void)settings;
   (void)type;
   (void)current;
   (void)min;
   (void)average;
   (void)max;
#endif
}

static char HardwareSensorDynamicMeter_temperatureUnit(
   const Settings* settings,
   HardwareSensorType type) {

   char unit = 'C';

#ifdef BUILD_WITH_CPU_TEMP
   if (type == SENSOR_TEMPERATURE && settings->degreeFahrenheit)
      unit = 'F';
#else
   (void)settings;
   (void)type;
#endif

   return unit;
}

static void HardwareSensorDynamicMeter_getValues(
   const HardwareSensor* sensor,
   double values[4]) {

   values[0] = sensor->value;

   if (sensor->sampleCount > 0) {
      values[1] = sensor->min;
      values[2] = sensor->average;
      values[3] = sensor->max;
   } else {
      values[1] = sensor->value;
      values[2] = sensor->value;
      values[3] = sensor->value;
   }
}

static void HardwareSensorDynamicMeter_getColumnWidths(
   const LinuxMachine* host,
   const Settings* settings,
   size_t widths[4]) {

   for (size_t i = 0; i < 4; i++)
      widths[i] = 0;

   for (size_t i = 0; i < host->sensorCount; i++) {
      const HardwareSensor* sensor = &host->sensors[i];

      double values[4];
      HardwareSensorDynamicMeter_getValues(sensor, values);

      HardwareSensorDynamicMeter_convertTemperature(
         settings,
         sensor->type,
         &values[0],
         &values[1],
         &values[2],
         &values[3]);

      char temperatureUnit =
         HardwareSensorDynamicMeter_temperatureUnit(settings, sensor->type);

      for (size_t column = 0; column < 4; column++) {
         char buffer[48];

         size_t width = HardwareSensorDynamicMeter_formatValue(
            buffer,
            sizeof(buffer),
            sensor->type,
            values[column],
            temperatureUnit);

         widths[column] = MAXIMUM(widths[column], width);
      }
   }
}

static void HardwareSensorDynamicMeter_appendPadding(
   RichString* out,
   size_t count) {

   if (count > 0)
      RichString_appendChr(out, CRT_colors[METER_TEXT], ' ', (int)count);
}

static void HardwareSensorDynamicMeter_appendAlignedValue(
   RichString* out,
   HardwareSensorType type,
   double value,
   char temperatureUnit,
   size_t width,
   int attribute) {

   char buffer[48];

   size_t valueWidth = HardwareSensorDynamicMeter_formatValue(
      buffer,
      sizeof(buffer),
      type,
      value,
      temperatureUnit);

   if (width > valueWidth)
      HardwareSensorDynamicMeter_appendPadding(out, width - valueWidth);

   RichString_appendWide(out, CRT_colors[attribute], buffer);
}

Hashtable* HardwareSensorDynamicMeters_new(void) {
   Hashtable* table = Hashtable_new(0, true);

   size_t sensorCount = 0;
   HardwareSensor* sensors = LibSensors_getHardwareSensors(&sensorCount);

   char** displayNames = xCalloc(sensorCount + 1, sizeof(char*));
   size_t maxDisplayNameWidth = 0;

   for (size_t i = 0; i < sensorCount; i++) {
      char name[256];

      HardwareSensorDynamicMeter_formatFriendlyName(
         sensors, sensorCount, i, name, sizeof(name));

      displayNames[i] = xStrdup(name);

      if (sensors[i].id)
         maxDisplayNameWidth = MAXIMUM(maxDisplayNameWidth, strlen(name));
   }

   ht_key_t key = 0;

   for (size_t i = 0; i < sensorCount; i++) {
      const HardwareSensor* sensor = &sensors[i];

      if (!sensor->id)
         continue;

      char stableName[32];
      HardwareSensorDynamicMeter_makeStableName(stableName, sensor->id);

      /*
       * A duplicate canonical ID should never occur. Avoid creating an
       * ambiguous persisted DynamicMeter if it nevertheless does.
       */
      if (DynamicMeter_search(table, stableName, NULL))
         continue;

      HardwareSensorDynamicMeter* meter = xCalloc(1, sizeof(*meter));

      xSnprintf(meter->super.name, sizeof(meter->super.name), "%s", stableName);
      xAsprintf(&meter->super.caption, "%s: ", displayNames[i]);
      xAsprintf(&meter->super.description, "Sensor: %s", displayNames[i]);

      meter->super.type = TEXT_METERMODE;
      meter->super.maximum = 0.0;

      meter->sensorId = xStrdup(sensor->id);
      meter->displayNameWidth = strlen(displayNames[i]);
      meter->maxDisplayNameWidth = maxDisplayNameWidth;

      Hashtable_put(table, ++key, meter);
   }

   String_freeArray(displayNames);
   LibSensors_freeHardwareSensors(sensors, sensorCount);

   return table;
}

static void HardwareSensorDynamicMeters_freeFields(
   ATTR_UNUSED ht_key_t key,
   void* value,
   ATTR_UNUSED void* data) {

   HardwareSensorDynamicMeter* meter = (HardwareSensorDynamicMeter*)value;

   free(meter->sensorId);
   free(meter->super.caption);
   free(meter->super.description);
}

void HardwareSensorDynamicMeters_done(Hashtable* table) {
   if (table)
      Hashtable_foreach(table, HardwareSensorDynamicMeters_freeFields, NULL);
}

void HardwareSensorDynamicMeter_init(Meter* meter) {
   (void)meter;
   hardwareSensorSamplingRequested = true;
}

void HardwareSensorDynamicMeter_updateValues(Meter* meter) {
   hardwareSensorSamplingRequested = true;
   meter->txtBuffer[0] = '\0';
}

bool HardwareSensorDynamicMeter_consumeSamplingRequest(void) {
   bool requested = hardwareSensorSamplingRequested;
   hardwareSensorSamplingRequested = false;
   return requested;
}

void HardwareSensorDynamicMeter_display(const Meter* meter, RichString* out) {
   const HardwareSensorDynamicMeter* definition =
      HardwareSensorDynamicMeter_lookupDefinition(meter);

   const HardwareSensor* sensor =
      HardwareSensorDynamicMeter_lookupSensor(meter, definition);

   if (!definition || !sensor) {
      RichString_writeAscii(out, CRT_colors[METER_VALUE_ERROR], "unavailable");
      return;
   }

   const LinuxMachine* host = (const LinuxMachine*)meter->host;
   const Settings* settings = meter->host->settings;

   if (definition->maxDisplayNameWidth > definition->displayNameWidth) {
      HardwareSensorDynamicMeter_appendPadding(
         out,
         definition->maxDisplayNameWidth - definition->displayNameWidth);
   }

   double values[4];
   HardwareSensorDynamicMeter_getValues(sensor, values);

   HardwareSensorDynamicMeter_convertTemperature(
      settings,
      sensor->type,
      &values[0],
      &values[1],
      &values[2],
      &values[3]);

   char temperatureUnit =
      HardwareSensorDynamicMeter_temperatureUnit(settings, sensor->type);

   size_t widths[4];
   HardwareSensorDynamicMeter_getColumnWidths(host, settings, widths);

   HardwareSensorDynamicMeter_appendAlignedValue(
      out, sensor->type, values[0],
      temperatureUnit, widths[0], METER_VALUE);

   RichString_appendWide(out, CRT_colors[METER_TEXT], " │ ");

   RichString_appendAscii(out, CRT_colors[METER_TEXT], "min ");
   HardwareSensorDynamicMeter_appendAlignedValue(
      out, sensor->type, values[1],
      temperatureUnit, widths[1], METER_VALUE_OK);

   RichString_appendAscii(out, CRT_colors[METER_TEXT], "   avg ");
   HardwareSensorDynamicMeter_appendAlignedValue(
      out, sensor->type, values[2],
      temperatureUnit, widths[2], METER_VALUE_NOTICE);

   RichString_appendAscii(out, CRT_colors[METER_TEXT], "   max ");
   HardwareSensorDynamicMeter_appendAlignedValue(
      out, sensor->type, values[3],
      temperatureUnit, widths[3], METER_VALUE_WARN);
}

#endif
