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


static size_t hardwareSensorMeterActive;

bool HardwareSensorMeter_active(void) {
   return hardwareSensorMeterActive > 0;
}

static void HardwareSensorMeter_init(Meter* this) {
   hardwareSensorMeterActive++;

#if defined(HTOP_LINUX) && defined(HAVE_SENSORS_SENSORS_H)
   if (this->param > 0) {
      size_t index = this->param - 1U;
      const char* chip = NULL;
      const char* label = NULL;
      const char* feature = NULL;
      HardwareSensorType type;

      if (Platform_getHardwareSensor(this->host, index, NULL, &chip, &label, &feature, &type, NULL)) {
         char name[64];
         StatusBar_formatSensorName(name, sizeof(name), chip, label, feature, type);

         size_t maxNameLength = strlen(name);
         size_t count = Platform_getHardwareSensorCount(this->host);

         for (size_t i = 0; i < count; i++) {
            const char* otherChip = NULL;
            const char* otherLabel = NULL;
            const char* otherFeature = NULL;
            HardwareSensorType otherType;

            if (!Platform_getHardwareSensor(this->host, i, NULL,
                                            &otherChip, &otherLabel, &otherFeature,
                                            &otherType, NULL))
               continue;

            char otherName[64];
            StatusBar_formatSensorName(otherName, sizeof(otherName),
                                       otherChip, otherLabel, otherFeature, otherType);

            maxNameLength = MAXIMUM(maxNameLength, strlen(otherName));
         }

         char labelWithColon[72];
         char caption[80];

         xSnprintf(labelWithColon, sizeof(labelWithColon), "%s:", name);

         /*
          * Pad every sensor caption to the longest discovered sensor name.
          * This keeps min/avg/max vertically aligned in the header.
          */
         int captionWidth = (int)MINIMUM(maxNameLength + 2, sizeof(caption) - 1);
         xSnprintf(caption, sizeof(caption), "%-*s", captionWidth, labelWithColon);

         Meter_setCaption(this, caption);
      }
   }
#endif
}

static void HardwareSensorMeter_done(Meter* this ATTR_UNUSED) {
   assert(hardwareSensorMeterActive > 0);
   hardwareSensorMeterActive--;
}

static void HardwareSensorMeter_getUiName(const Meter* this, char* name, size_t length) {
#if defined(HTOP_LINUX) && defined(HAVE_SENSORS_SENSORS_H)
   if (this->param > 0) {
      size_t index = this->param - 1U;
      const char* chip = NULL;
      const char* label = NULL;
      const char* feature = NULL;
      HardwareSensorType type;

      if (Platform_getHardwareSensor(this->host, index, NULL, &chip, &label, &feature, &type, NULL)) {
         StatusBar_formatSensorName(name, length, chip, label, feature, type);
         return;
      }
   }
#endif

   xSnprintf(name, length, "Hardware sensor");
}

static size_t HardwareSensorMeter_formatValue(char* buffer,
                                              size_t size,
                                              HardwareSensorType type,
                                              double value,
                                              char temperatureUnit) {
   char number[32];
   xSnprintf(number, sizeof(number), "%.0f", value);

   switch (type) {
      case SENSOR_TEMPERATURE:
         xSnprintf(buffer, size, "%s%s%c", number, CRT_degreeSign, temperatureUnit);
         /* Degree sign is one terminal column even though UTF-8 uses multiple bytes. */
         return strlen(number) + 2;

      case SENSOR_FAN:
         xSnprintf(buffer, size, "%s RPM", number);
         return strlen(buffer);

      default:
         xSnprintf(buffer, size, "%s", number);
         return strlen(buffer);
   }
}

static void HardwareSensorMeter_convertTemperature(const Settings* settings,
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

static void HardwareSensorMeter_getColumnWidths(const Meter* this, size_t widths[4]) {
   for (size_t i = 0; i < 4; i++)
      widths[i] = 0;

#if defined(HTOP_LINUX) && defined(HAVE_SENSORS_SENSORS_H)
   size_t count = Platform_getHardwareSensorCount(this->host);

   for (size_t i = 0; i < count; i++) {
      HardwareSensorType type;
      double current;

      if (!Platform_getHardwareSensor(this->host, i, NULL,
                                      NULL, NULL, NULL, &type, &current))
         continue;

      double min = current;
      double average = current;
      double max = current;

      Platform_getHardwareSensorStats(this->host, i, &min, &average, &max);

      HardwareSensorMeter_convertTemperature(this->host->settings, type,
                                             &current, &min, &average, &max);

      char temperatureUnit = 'C';

#ifdef BUILD_WITH_CPU_TEMP
      if (type == SENSOR_TEMPERATURE && this->host->settings->degreeFahrenheit)
         temperatureUnit = 'F';
#endif

      double values[4] = { current, min, average, max };

      for (size_t column = 0; column < 4; column++) {
         char buffer[48];
         size_t width = HardwareSensorMeter_formatValue(buffer, sizeof(buffer),
                                                        type, values[column],
                                                        temperatureUnit);
         widths[column] = MAXIMUM(widths[column], width);
      }
   }
#else
   (void)this;
#endif
}

static void HardwareSensorMeter_updateValues(Meter* this) {
   this->txtBuffer[0] = '\0';
   this->curItems = 0;

#if defined(HTOP_LINUX) && defined(HAVE_SENSORS_SENSORS_H)
   if (this->param == 0)
      return;

   size_t index = this->param - 1U;

   HardwareSensorType type;
   double current;

   if (!Platform_getHardwareSensor(this->host, index, NULL,
                                   NULL, NULL, NULL, &type, &current))
      return;

   double min = current;
   double average = current;
   double max = current;

   Platform_getHardwareSensorStats(this->host, index, &min, &average, &max);

   HardwareSensorMeter_convertTemperature(this->host->settings, type,
                                          &current, &min, &average, &max);

   this->values[0] = current;
   this->values[1] = min;
   this->values[2] = average;
   this->values[3] = max;
   this->curItems = 4;
#endif
}

static void HardwareSensorMeter_appendPadding(RichString* out, size_t count) {
   if (count > 0)
      RichString_appendChr(out, CRT_colors[METER_TEXT], ' ', (int)count);
}

static void HardwareSensorMeter_appendAlignedValue(RichString* out,
                                                   HardwareSensorType type,
                                                   double value,
                                                   char temperatureUnit,
                                                   size_t width,
                                                   int attribute) {
   char buffer[48];

   size_t valueWidth = HardwareSensorMeter_formatValue(buffer, sizeof(buffer),
                                                       type, value,
                                                       temperatureUnit);

   if (width > valueWidth)
      HardwareSensorMeter_appendPadding(out, width - valueWidth);

   RichString_appendWide(out, CRT_colors[attribute], buffer);
}

static void HardwareSensorMeter_display(const Object* cast, RichString* out) {
   const Meter* this = (const Meter*) cast;

#if defined(HTOP_LINUX) && defined(HAVE_SENSORS_SENSORS_H)
   if (this->param == 0 || this->curItems < 4) {
      RichString_writeAscii(out, CRT_colors[METER_VALUE_ERROR], "unavailable");
      return;
   }

   size_t index = this->param - 1U;
   HardwareSensorType type;

   if (!Platform_getHardwareSensor(this->host, index, NULL,
                                   NULL, NULL, NULL, &type, NULL)) {
      RichString_writeAscii(out, CRT_colors[METER_VALUE_ERROR], "unavailable");
      return;
   }

   char temperatureUnit = 'C';

#ifdef BUILD_WITH_CPU_TEMP
   if (type == SENSOR_TEMPERATURE && this->host->settings->degreeFahrenheit)
      temperatureUnit = 'F';
#endif

   size_t widths[4];
   HardwareSensorMeter_getColumnWidths(this, widths);

   /* Current value */
   HardwareSensorMeter_appendAlignedValue(out, type, this->values[0],
                                          temperatureUnit, widths[0],
                                          METER_VALUE);

   /* Visual separator: equivalent to a one-character left border in a TUI. */
   RichString_appendWide(out, CRT_colors[METER_TEXT], " │ ");

   /* Minimum */
   RichString_appendAscii(out, CRT_colors[METER_TEXT], "min ");
   HardwareSensorMeter_appendAlignedValue(out, type, this->values[1],
                                          temperatureUnit, widths[1],
                                          METER_VALUE_OK);

   /* Average */
   RichString_appendAscii(out, CRT_colors[METER_TEXT], "   avg ");
   HardwareSensorMeter_appendAlignedValue(out, type, this->values[2],
                                          temperatureUnit, widths[2],
                                          METER_VALUE_NOTICE);

   /* Maximum */
   RichString_appendAscii(out, CRT_colors[METER_TEXT], "   max ");
   HardwareSensorMeter_appendAlignedValue(out, type, this->values[3],
                                          temperatureUnit, widths[3],
                                          METER_VALUE_WARN);
#else
   RichString_writeAscii(out, CRT_colors[METER_VALUE_ERROR], "unavailable");
#endif
}


static const int HardwareSensorMeter_attributes[] = {
   METER_VALUE,
   METER_VALUE_OK,
   METER_VALUE_NOTICE,
   METER_VALUE_WARN
};

const MeterClass HardwareSensorMeter_class = {
   .super = {
      .extends = Class(Meter),
      .delete = Meter_delete,
      .display = HardwareSensorMeter_display
   },
   .init = HardwareSensorMeter_init,
   .done = HardwareSensorMeter_done,
   .updateValues = HardwareSensorMeter_updateValues,
   .getUiName = HardwareSensorMeter_getUiName,
   .defaultMode = TEXT_METERMODE,
   .supportedModes = (1U << TEXT_METERMODE),
   .maxItems = 4,
   .isPercentChart = false,
   .total = 100.0,
   .attributes = HardwareSensorMeter_attributes,
   .name = "HardwareSensor",
   .uiName = "Hardware sensor",
   .caption = "Sensor: "
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
