/*
htop - StatusBar.c
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h" // IWYU pragma: keep

#include "StatusBar.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "CRT.h"
#include "Macros.h"
#include "Platform.h"
#include "ProvideCurses.h"
#include "RichString.h"
#include "Settings.h"
#include "XUtils.h"


void StatusBar_formatSensorName(char* buffer, size_t size, const char* chip, const char* label, const char* feature, HardwareSensorType type) {
   if (label && label[0]) {
      xSnprintf(buffer, size, "%s", label);
      return;
   }

   if (type == SENSOR_FAN && feature && String_startsWith(feature, "fan") && feature[3]) {
      xSnprintf(buffer, size, "Fan %s", feature + 3);
      return;
   }

   const char* source = chip;
   if (!source || !source[0])
      source = "sensor";

   xSnprintf(buffer, size, "%s", source);

   for (char* p = buffer; *p; p++) {
      if (*p == '_' || *p == '-')
         *p = ' ';
   }

   static const char thermalSuffix[] = " thermal";
   const size_t thermalSuffixLen = sizeof(thermalSuffix) - 1;
   size_t len = strlen(buffer);
   if (len > thermalSuffixLen && String_eq(buffer + len - thermalSuffixLen, thermalSuffix))
      buffer[len - thermalSuffixLen] = '\0';

   for (char* token = buffer; *token;) {
      while (*token == ' ')
         token++;
      if (!*token)
         break;

      char* end = token;
      while (*end && *end != ' ')
         end++;

      // Short tokens are typically hardware abbreviations such as CPU, GPU, or PCH.
      if ((size_t)(end - token) <= 3) {
         for (char* p = token; p < end; p++) {
            if (*p >= 'a' && *p <= 'z')
               *p -= 'a' - 'A';
         }
      }

      token = end;
   }
}

#if defined(HTOP_LINUX) && defined(HAVE_SENSORS_SENSORS_H)
static void StatusBar_appendSensorValue(char* buffer, size_t size, HardwareSensorType type, const char* label, double value, char temperatureUnit) {
   size_t used = strlen(buffer);
   if (used >= size - 1)
      return;

   switch (type) {
   case SENSOR_TEMPERATURE:
      xSnprintf(buffer + used, size - used, " %s%.0f%s%c", label, value, CRT_degreeSign, temperatureUnit);
      break;
   case SENSOR_FAN:
      xSnprintf(buffer + used, size - used, " %s%.0f RPM", label, value);
      break;
   default:
      xSnprintf(buffer + used, size - used, " %s%.0f", label, value);
      break;
   }
}

static int StatusBar_drawSensor(const Machine* host, size_t index, bool showMin, bool showAverage, bool showMax, int y, int x) {
   char buffer[256];
   const char* chip = NULL;
   const char* label = NULL;
   const char* feature = NULL;
   HardwareSensorType type;
   double current;
   double min;
   double average;
   double max;

   if (!Platform_getHardwareSensor(host, index, NULL, &chip, &label, &feature, &type, &current))
      return x;

   min = current;
   average = current;
   max = current;
   Platform_getHardwareSensorStats(host, index, &min, &average, &max);

   char name[64];
   StatusBar_formatSensorName(name, sizeof(name), chip, label, feature, type);

   char temperatureUnit = 'C';
   if (type == SENSOR_TEMPERATURE) {
#ifdef BUILD_WITH_CPU_TEMP
      if (host->settings->degreeFahrenheit) {
         current = current * 9 / 5 + 32;
         min = min * 9 / 5 + 32;
         average = average * 9 / 5 + 32;
         max = max * 9 / 5 + 32;
         temperatureUnit = 'F';
      }
#endif
   }

   xSnprintf(buffer, sizeof(buffer), " | %s", name);

   StatusBar_appendSensorValue(buffer, sizeof(buffer), type, "", current, temperatureUnit);
   if (showMin)
      StatusBar_appendSensorValue(buffer, sizeof(buffer), type, "min ", min, temperatureUnit);
   if (showAverage)
      StatusBar_appendSensorValue(buffer, sizeof(buffer), type, "avg ", average, temperatureUnit);
   if (showMax)
      StatusBar_appendSensorValue(buffer, sizeof(buffer), type, "max ", max, temperatureUnit);

   int columns = COLS - x;
   if (columns <= 0)
      return x;

   RichString_begin(output);
   RichString_appendnWideColumns(&output, CRT_colors[PANEL_HEADER_FOCUS], buffer, strlen(buffer), &columns);
   RichString_printVal(output, y, x);
   RichString_delete(&output);

   return x + columns;
}
#endif


static size_t hardwareSensorsMeterActive;

bool HardwareSensorsMeter_active(void) {
   return hardwareSensorsMeterActive > 0;
}

static void HardwareSensorsMeter_init(Meter* this ATTR_UNUSED) {
   hardwareSensorsMeterActive++;
}

static void HardwareSensorsMeter_done(Meter* this ATTR_UNUSED) {
   assert(hardwareSensorsMeterActive > 0);
   hardwareSensorsMeterActive--;
}

#if defined(HTOP_LINUX) && defined(HAVE_SENSORS_SENSORS_H)
static bool HardwareSensorsMeter_appendSensor(const Machine* host, size_t index, bool showMin, bool showAverage, bool showMax, char* buffer, size_t size) {
   const char* chip = NULL;
   const char* label = NULL;
   const char* feature = NULL;
   HardwareSensorType type;
   double current;
   double min;
   double average;
   double max;

   if (!Platform_getHardwareSensor(host, index, NULL, &chip, &label, &feature, &type, &current))
      return false;

   min = current;
   average = current;
   max = current;
   Platform_getHardwareSensorStats(host, index, &min, &average, &max);

   char name[64];
   StatusBar_formatSensorName(name, sizeof(name), chip, label, feature, type);

   char temperatureUnit = 'C';
   if (type == SENSOR_TEMPERATURE) {
#ifdef BUILD_WITH_CPU_TEMP
      if (host->settings->degreeFahrenheit) {
         current = current * 9 / 5 + 32;
         min = min * 9 / 5 + 32;
         average = average * 9 / 5 + 32;
         max = max * 9 / 5 + 32;
         temperatureUnit = 'F';
      }
#endif
   }

   size_t used = strlen(buffer);
   if (used >= size - 1)
      return false;

   xSnprintf(buffer + used, size - used, "%s%s", used ? " | " : "", name);

   StatusBar_appendSensorValue(buffer, size, type, "", current, temperatureUnit);
   if (showMin)
      StatusBar_appendSensorValue(buffer, size, type, "min ", min, temperatureUnit);
   if (showAverage)
      StatusBar_appendSensorValue(buffer, size, type, "avg ", average, temperatureUnit);
   if (showMax)
      StatusBar_appendSensorValue(buffer, size, type, "max ", max, temperatureUnit);

   return true;
}
#endif

static void HardwareSensorsMeter_updateValues(Meter* this) {
   this->txtBuffer[0] = '\0';
   this->curItems = 0;

#if defined(HTOP_LINUX) && defined(HAVE_SENSORS_SENSORS_H)
   const Machine* host = this->host;
   const Settings* settings = host->settings;
   size_t count = Platform_getHardwareSensorCount(host);
   size_t added = 0;

   if (!settings->statusBarSensorsConfigured) {
      for (size_t i = 0; i < count; i++) {
         if (HardwareSensorsMeter_appendSensor(host, i, false, false, false,
                                               this->txtBuffer, sizeof(this->txtBuffer)))
            added++;
      }
   } else {
      for (size_t selection = 0; selection < settings->statusBarSensorCount; selection++) {
         const StatusBarSensorConfig* config = &settings->statusBarSensors[selection];
         if (!config->enabled)
            continue;

         for (size_t i = 0; i < count; i++) {
            const char* id = NULL;
            if (!Platform_getHardwareSensor(host, i, &id, NULL, NULL, NULL, NULL, NULL))
               continue;

            if (id && String_eq(id, config->id)) {
               if (HardwareSensorsMeter_appendSensor(host, i,
                                                     config->showMin,
                                                     config->showAverage,
                                                     config->showMax,
                                                     this->txtBuffer,
                                                     sizeof(this->txtBuffer)))
                  added++;
               break;
            }
         }
      }
   }

   if (!added)
      xSnprintf(this->txtBuffer, sizeof(this->txtBuffer), "none selected");
#else
   xSnprintf(this->txtBuffer, sizeof(this->txtBuffer), "unavailable");
#endif
}

static const int HardwareSensorsMeter_attributes[] = {
   METER_VALUE
};

const MeterClass HardwareSensorsMeter_class = {
   .super = {
      .extends = Class(Meter),
      .delete = Meter_delete
   },
   .init = HardwareSensorsMeter_init,
   .done = HardwareSensorsMeter_done,
   .updateValues = HardwareSensorsMeter_updateValues,
   .defaultMode = TEXT_METERMODE,
   .supportedModes = (1U << TEXT_METERMODE),
   .maxItems = 1,
   .isPercentChart = false,
   .total = 100.0,
   .attributes = HardwareSensorsMeter_attributes,
   .name = "HardwareSensors",
   .uiName = "Hardware sensors (prototype)",
   .caption = "Sensors: "
};

void StatusBar_draw(const Machine* host, int y) {
   if (y < 0)
      return;

   attrset(CRT_colors[PANEL_HEADER_FOCUS]);
   mvhline(y, 0, ' ', COLS);

#if defined(HTOP_LINUX) && defined(HAVE_SENSORS_SENSORS_H)
   size_t count = Platform_getHardwareSensorCount(host);
   int x = 1;

   if (COLS > 2) {
      const char* title = count ? "SENSORS" : "SENSORS | no sensors";
      int len = MINIMUM((int)strlen(title), COLS - x);
      mvaddnstr(y, x, title, len);
      x += len;
   }

   const Settings* settings = host->settings;
   if (!settings->statusBarSensorsConfigured) {
      for (size_t i = 0; i < count && x < COLS - 1; i++)
         x = StatusBar_drawSensor(host, i, false, false, false, y, x);
   } else {
      for (size_t selection = 0; selection < settings->statusBarSensorCount && x < COLS - 1; selection++) {
         const StatusBarSensorConfig* config = &settings->statusBarSensors[selection];
         if (!config->enabled)
            continue;

         for (size_t i = 0; i < count; i++) {
            const char* id = NULL;
            if (!Platform_getHardwareSensor(host, i, &id, NULL, NULL, NULL, NULL, NULL))
               continue;
            if (id && String_eq(id, config->id)) {
               x = StatusBar_drawSensor(host, i, config->showMin, config->showAverage, config->showMax, y, x);
               break;
            }
         }
      }
   }
#else
   (void)host;
   if (COLS > 2)
      mvaddnstr(y, 1, "SENSORS | unavailable", COLS - 2);
#endif

   attrset(CRT_colors[RESET_COLOR]);
}
