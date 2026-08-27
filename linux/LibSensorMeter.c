/*
htop - linux/LibSensorMeter.c
(C) 2026 Massimo Mazzariol
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h" // IWYU pragma: keep

#include "linux/LibSensorMeter.h"

#ifdef HAVE_SENSORS_SENSORS_H

#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "CRT.h"
#include "DynamicMeter.h"
#include "HardwareSensor.h"
#include "Macros.h"
#include "Settings.h"
#include "XUtils.h"
#include "linux/LibSensors.h"
#include "linux/LinuxMachine.h"

static bool hardwareSensorSamplingRequested = false;
static size_t hardwareSensorVisibleLabelWidth = 0;
static uint64_t hardwareSensorLayoutVersion = 0;

static const size_t dynamicMeterNameMax = 29;
static const size_t sensorDisplayLabelMax = 31; /* Meter_toListItem() uses char name[32]. */

static char* LibSensorMeter_getUserLabelOverridePath(void) {
   const char* xdgConfigHome = getenv("XDG_CONFIG_HOME");
   if (xdgConfigHome && xdgConfigHome[0] == '/')
      return String_cat(xdgConfigHome, "/htop/sensor-labels.conf");

   const char* home = getenv("HOME");
   if (!home || home[0] != '/') {
      const struct passwd* pw = getpwuid(getuid());
      home = (pw && pw->pw_dir && pw->pw_dir[0] == '/') ? pw->pw_dir : NULL;
   }

   return home ? String_cat(home, CONFIGDIR "/htop/sensor-labels.conf") : NULL;
}

static void LibSensorMeter_applyLabelOverrides(const char* path, HardwareSensor* sensors, size_t sensorCount) {
   if (!path)
      return;

   FILE* file = fopen(path, "r");
   if (!file)
      return;

   for (;;) {
      char* line = String_readLine(file);
      if (!line)
         break;

      char* trimmed = String_trim(line);
      free(line);

      if (trimmed[0] == '#' || trimmed[0] == '\0') {
         free(trimmed);
         continue;
      }

      size_t n;
      char** config = String_splitFirst(trimmed, '=', &n);
      free(trimmed);
      if (!config)
         continue;

      char* id = String_trim(config[0]);
      char* label = n > 1 ? String_trim(config[1]) : NULL;

      if (id[0] && label && label[0]) {
         for (size_t i = 0; i < sensorCount; i++) {
            if (String_eq(sensors[i].id, id)) {
               free(sensors[i].label);
               sensors[i].label = xStrdup(label);
               sensors[i].labelSource = SENSOR_LABEL_HTOP;
               break;
            }
         }
      }

      free(label);
      free(id);
      String_freeArray(config);
   }

   fclose(file);
}

static const char* LibSensorMeter_labelSourceName(HardwareSensorLabelSource source) {
   switch (source) {
      case SENSOR_LABEL_HTOP:
         return "htop label";
      case SENSOR_LABEL_LIBSENSORS:
         return "libsensors label";
      case SENSOR_LABEL_FALLBACK:
         return "fallback";
   }

   return "fallback";
}

static const DynamicMeter* LibSensorMeter_lookupDefinition(const Meter* meter) {
   return Hashtable_get(meter->host->settings->dynamicMeters, meter->param);
}

static size_t LibSensorMeter_displayNameLength(const DynamicMeter* definition) {
   if (!definition || !definition->caption)
      return 0;

   const size_t captionLength = strlen(definition->caption);
   return captionLength >= 2 ? captionLength - 2 : captionLength;
}

static void LibSensorMeter_updateVisibleLabelWidth(const Meter* meter) {
   const Settings* settings = meter->host->settings;

   if (hardwareSensorLayoutVersion != settings->lastUpdate) {
      hardwareSensorLayoutVersion = settings->lastUpdate;
      hardwareSensorVisibleLabelWidth = 0;
   }

   const DynamicMeter* definition = LibSensorMeter_lookupDefinition(meter);
   hardwareSensorVisibleLabelWidth =
      MAXIMUM(hardwareSensorVisibleLabelWidth, LibSensorMeter_displayNameLength(definition));
}

static const HardwareSensor* LibSensorMeter_lookupSensor(const Meter* meter, const DynamicMeter* definition) {
   const LinuxMachine* host = (const LinuxMachine*)meter->host;

   if (!definition)
      return NULL;

   for (size_t i = 0; i < host->sensorCount; i++) {
      const HardwareSensor* sensor = &host->sensors[i];

      if (String_eq(sensor->id, definition->name))
         return sensor;
   }

   return NULL;
}

static void LibSensorMeter_formatValue(char* buffer, size_t size, const Settings* settings, HardwareSensorType type, double value) {
   char unit = 'C';

#ifdef BUILD_WITH_CPU_TEMP
   if (type == SENSOR_TEMPERATURE && settings->degreeFahrenheit) {
      value = value * 9 / 5 + 32;
      unit = 'F';
   }
#else
   (void)settings;
#endif

   switch (type) {
      case SENSOR_TEMPERATURE:
         xSnprintf(buffer, size, "%*d%s%c", CRT_degreeSign[0] ? 7 : 8, (int)value, CRT_degreeSign, unit);
         return;

      case SENSOR_FAN:
         xSnprintf(buffer, size, "%5.0f RPM", value);
         return;
   }
}

static void LibSensorMeter_appendSeparator(RichString* out) {

#ifdef HAVE_LIBNCURSESW
   if (CRT_utf8) {
      RichString_appendWide(out, CRT_colors[METER_TEXT], " │ ");
      return;
   }
#endif

   RichString_appendAscii(out, CRT_colors[METER_TEXT], " | ");
}

static void LibSensorMeter_appendValue(RichString* out, const Settings* settings, HardwareSensorType type, double value, int attribute) {
   char buffer[48];
   LibSensorMeter_formatValue(buffer, sizeof(buffer), settings, type, value);
   RichString_appendWide(out, CRT_colors[attribute], buffer);
}

Hashtable* LibSensorMeter_new(void) {
   Hashtable* table = Hashtable_new(0, true);

   size_t sensorCount = 0;
   HardwareSensor* sensors = LibSensors_getHardwareSensors(&sensorCount);

   LibSensorMeter_applyLabelOverrides(SYSCONFDIR "/htop/sensor-labels.conf", sensors, sensorCount);

   char* userLabelPath = LibSensorMeter_getUserLabelOverridePath();
   LibSensorMeter_applyLabelOverrides(userLabelPath, sensors, sensorCount);
   free(userLabelPath);

   ht_key_t key = 0;

   for (size_t i = 0; i < sensorCount; i++) {
      const HardwareSensor* sensor = &sensors[i];

      /*
       * Header's Dynamic(...) parser includes the closing parenthesis in its
       * 30-byte scanf field, leaving 29 bytes for the persisted name.
       */
      if (strlen(sensor->id) > dynamicMeterNameMax)
         continue;

      DynamicMeter* meter = xCalloc(1, sizeof(*meter));

      String_safeStrncpy(meter->name, sensor->id, sizeof(meter->name));
      const char* rawDisplayName = sensor->label ? sensor->label : sensor->id;
      char* displayName = xStrndup(rawDisplayName, sensorDisplayLabelMax);

      xAsprintf(&meter->caption, "%s: ", displayName);
      xAsprintf(&meter->description, "%s [%s] (%s)",
                displayName, LibSensorMeter_labelSourceName(sensor->labelSource), sensor->id);
      free(displayName);

      meter->type = TEXT_METERMODE;

      Hashtable_put(table, ++key, meter);
   }

   LibSensors_freeHardwareSensors(sensors, sensorCount);

   return table;
}

static void LibSensorMeter_freeFields(ATTR_UNUSED ht_key_t key, void* value, ATTR_UNUSED void* data) {

   DynamicMeter* meter = (DynamicMeter*)value;

   free(meter->caption);
   free(meter->description);
}

void LibSensorMeter_done(Hashtable* table) {
   if (table)
      Hashtable_foreach(table, LibSensorMeter_freeFields, NULL);
}

void LibSensorMeter_init(Meter* meter) {
   LibSensorMeter_updateVisibleLabelWidth(meter);
   hardwareSensorSamplingRequested = true;
}

void LibSensorMeter_updateValues(Meter* meter) {
   LibSensorMeter_updateVisibleLabelWidth(meter);
   hardwareSensorSamplingRequested = true;
   meter->txtBuffer[0] = '\0';
}

bool LibSensorMeter_consumeSamplingRequest(void) {
   const bool requested = hardwareSensorSamplingRequested;
   hardwareSensorSamplingRequested = false;
   return requested;
}

void LibSensorMeter_display(const Meter* meter, RichString* out) {
   const DynamicMeter* definition = LibSensorMeter_lookupDefinition(meter);
   const HardwareSensor* sensor = LibSensorMeter_lookupSensor(meter, definition);

   if (!definition || !sensor) {
      RichString_writeAscii(out, CRT_colors[METER_VALUE_ERROR], "unavailable");
      return;
   }

   const Settings* settings = meter->host->settings;
   const size_t displayNameLength = LibSensorMeter_displayNameLength(definition);
   const size_t captionPadding =
      hardwareSensorVisibleLabelWidth > displayNameLength
         ? hardwareSensorVisibleLabelWidth - displayNameLength
         : 0;

   for (size_t i = 0; i < captionPadding; i++)
      RichString_appendChr(out, CRT_colors[METER_TEXT], ' ', 1);

   LibSensorMeter_appendValue(out, settings, sensor->type, sensor->value, METER_VALUE);

   LibSensorMeter_appendSeparator(out);

   RichString_appendAscii(out, CRT_colors[METER_TEXT], "min ");
   LibSensorMeter_appendValue(out, settings, sensor->type, sensor->min, METER_VALUE_OK);

   RichString_appendAscii(out, CRT_colors[METER_TEXT], "   avg ");
   LibSensorMeter_appendValue(out, settings, sensor->type, sensor->average, METER_VALUE_NOTICE);

   RichString_appendAscii(out, CRT_colors[METER_TEXT], "   max ");
   LibSensorMeter_appendValue(out, settings, sensor->type, sensor->max, METER_VALUE_WARN);
}

#endif
