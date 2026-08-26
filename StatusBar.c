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

static bool HardwareSensorsMeter_sensorIndexForRow(const Machine* host,
                                                   size_t row,
                                                   size_t* sensorIndex) {
#if defined(HTOP_LINUX) && defined(HAVE_SENSORS_SENSORS_H)
   const Settings* settings = host->settings;
   size_t count = Platform_getHardwareSensorCount(host);

   if (!settings->statusBarSensorsConfigured) {
      if (row >= count)
         return false;

      *sensorIndex = row;
      return true;
   }

   size_t visibleRow = 0;

   for (size_t selection = 0; selection < settings->statusBarSensorCount; selection++) {
      const StatusBarSensorConfig* config = &settings->statusBarSensors[selection];

      if (!config->enabled)
         continue;

      for (size_t i = 0; i < count; i++) {
         const char* id = NULL;

         if (!Platform_getHardwareSensor(host, i, &id,
                                         NULL, NULL, NULL, NULL, NULL))
            continue;

         if (!id || !config->id || !String_eq(id, config->id))
            continue;

         if (visibleRow == row) {
            *sensorIndex = i;
            return true;
         }

         visibleRow++;
         break;
      }
   }
#else
   (void)host;
   (void)row;
   (void)sensorIndex;
#endif

   return false;
}

static size_t HardwareSensorsMeter_rowCount(const Machine* host) {
#if defined(HTOP_LINUX) && defined(HAVE_SENSORS_SENSORS_H)
   const Settings* settings = host->settings;

   if (!settings->statusBarSensorsConfigured)
      return Platform_getHardwareSensorCount(host);

   size_t rows = 0;
   size_t count = Platform_getHardwareSensorCount(host);

   for (size_t selection = 0; selection < settings->statusBarSensorCount; selection++) {
      const StatusBarSensorConfig* config = &settings->statusBarSensors[selection];

      if (!config->enabled)
         continue;

      for (size_t i = 0; i < count; i++) {
         const char* id = NULL;

         if (!Platform_getHardwareSensor(host, i, &id,
                                         NULL, NULL, NULL, NULL, NULL))
            continue;

         if (id && config->id && String_eq(id, config->id)) {
            rows++;
            break;
         }
      }
   }

   return rows;
#else
   (void)host;
   return 0;
#endif
}

static size_t HardwareSensorsMeter_formatValue(char* buffer,
                                               size_t size,
                                               HardwareSensorType type,
                                               double value,
                                               char temperatureUnit) {
   char number[32];
   xSnprintf(number, sizeof(number), "%.0f", value);

   switch (type) {
      case SENSOR_TEMPERATURE:
         xSnprintf(buffer, size, "%s%s%c",
                   number, CRT_degreeSign, temperatureUnit);
         /* Degree sign is one terminal column although UTF-8 uses multiple bytes. */
         return strlen(number) + 2;

      case SENSOR_FAN:
         xSnprintf(buffer, size, "%s RPM", number);
         return strlen(buffer);

      default:
         xSnprintf(buffer, size, "%s", number);
         return strlen(buffer);
   }
}

static void HardwareSensorsMeter_convertTemperature(const Settings* settings,
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

static bool HardwareSensorsMeter_getSensorValues(const Machine* host,
                                                 size_t index,
                                                 char* name,
                                                 size_t nameSize,
                                                 HardwareSensorType* type,
                                                 double values[4],
                                                 char* temperatureUnit) {
#if defined(HTOP_LINUX) && defined(HAVE_SENSORS_SENSORS_H)
   const char* chip = NULL;
   const char* label = NULL;
   const char* feature = NULL;
   double current;

   if (!Platform_getHardwareSensor(host, index, NULL,
                                   &chip, &label, &feature,
                                   type, &current))
      return false;

   double min = current;
   double average = current;
   double max = current;

   Platform_getHardwareSensorStats(host, index, &min, &average, &max);

   StatusBar_formatSensorName(name, nameSize, chip, label, feature, *type);

   HardwareSensorsMeter_convertTemperature(host->settings, *type,
                                           &current, &min, &average, &max);

   *temperatureUnit = 'C';

#ifdef BUILD_WITH_CPU_TEMP
   if (*type == SENSOR_TEMPERATURE && host->settings->degreeFahrenheit)
      *temperatureUnit = 'F';
#endif

   values[0] = current;
   values[1] = min;
   values[2] = average;
   values[3] = max;

   return true;
#else
   (void)host;
   (void)index;
   (void)name;
   (void)nameSize;
   (void)type;
   (void)values;
   (void)temperatureUnit;
   return false;
#endif
}

static void HardwareSensorsMeter_getTableWidths(const Machine* host,
                                                size_t* nameWidth,
                                                size_t valueWidths[4]) {
   *nameWidth = 0;

   for (size_t i = 0; i < 4; i++)
      valueWidths[i] = 0;

   size_t rows = HardwareSensorsMeter_rowCount(host);

   for (size_t row = 0; row < rows; row++) {
      size_t index;

      if (!HardwareSensorsMeter_sensorIndexForRow(host, row, &index))
         continue;

      char name[64];
      HardwareSensorType type;
      double values[4];
      char temperatureUnit;

      if (!HardwareSensorsMeter_getSensorValues(host, index,
                                                name, sizeof(name),
                                                &type, values,
                                                &temperatureUnit))
         continue;

      *nameWidth = MAXIMUM(*nameWidth, strlen(name));

      for (size_t column = 0; column < 4; column++) {
         char buffer[48];

         size_t width = HardwareSensorsMeter_formatValue(
            buffer, sizeof(buffer), type, values[column], temperatureUnit);

         valueWidths[column] = MAXIMUM(valueWidths[column], width);
      }
   }
}

static void HardwareSensorsMeter_appendPadding(RichString* out, size_t count) {
   if (count)
      RichString_appendChr(out, CRT_colors[METER_TEXT], ' ', (int)count);
}

static void HardwareSensorsMeter_appendAlignedValue(RichString* out,
                                                    HardwareSensorType type,
                                                    double value,
                                                    char temperatureUnit,
                                                    size_t width,
                                                    int attribute) {
   char buffer[48];

   size_t valueWidth = HardwareSensorsMeter_formatValue(
      buffer, sizeof(buffer), type, value, temperatureUnit);

   if (width > valueWidth)
      HardwareSensorsMeter_appendPadding(out, width - valueWidth);

   RichString_appendWide(out, CRT_colors[attribute], buffer);
}

static void HardwareSensorsMeter_updateMode(Meter* this, MeterModeId mode) {
   (void)mode;

   size_t rows = HardwareSensorsMeter_rowCount(this->host);
   this->h = rows ? (int)rows : 1;
}

static void HardwareSensorsMeter_init(Meter* this ATTR_UNUSED) {
   hardwareSensorsMeterActive++;
}

static void HardwareSensorsMeter_done(Meter* this ATTR_UNUSED) {
   assert(hardwareSensorsMeterActive > 0);
   hardwareSensorsMeterActive--;
}

static void HardwareSensorsMeter_updateValues(Meter* this) {
   /*
    * Values are read from the shared sensor cache in draw().
    * The meter itself only controls polling activity and layout.
    */
   this->txtBuffer[0] = '\0';
   this->curItems = 0;
}

static void HardwareSensorsMeter_draw(Meter* this, int x, int y, int w) {
   if (w <= 0)
      return;

#if defined(HTOP_LINUX) && defined(HAVE_SENSORS_SENSORS_H)
   const Machine* host = this->host;
   size_t rows = HardwareSensorsMeter_rowCount(host);

   if (!rows) {
      attrset(CRT_colors[METER_TEXT]);
      mvaddnstr(y, x, "Sensors: ", w);

      if (w > 9) {
         attrset(CRT_colors[METER_VALUE_ERROR]);
         mvaddnstr(y, x + 9, "none selected", w - 9);
      }

      attrset(CRT_colors[RESET_COLOR]);
      return;
   }

   size_t nameWidth;
   size_t valueWidths[4];

   HardwareSensorsMeter_getTableWidths(host, &nameWidth, valueWidths);

   static const char title[] = "Sensors: ";
   const size_t titleWidth = sizeof(title) - 1;

   for (size_t row = 0; row < rows; row++) {
      size_t index;

      if (!HardwareSensorsMeter_sensorIndexForRow(host, row, &index))
         continue;

      char name[64];
      HardwareSensorType type;
      double values[4];
      char temperatureUnit;

      if (!HardwareSensorsMeter_getSensorValues(host, index,
                                                name, sizeof(name),
                                                &type, values,
                                                &temperatureUnit))
         continue;

      RichString_begin(line);

      if (row == 0)
         RichString_appendAscii(&line, CRT_colors[METER_TEXT], title);
      else
         HardwareSensorsMeter_appendPadding(&line, titleWidth);

      char label[72];
      xSnprintf(label, sizeof(label), "%s:", name);

      RichString_appendAscii(&line, CRT_colors[METER_TEXT], label);

      size_t labelWidth = strlen(label);
      size_t targetLabelWidth = nameWidth + 2;

      if (targetLabelWidth > labelWidth)
         HardwareSensorsMeter_appendPadding(&line,
                                            targetLabelWidth - labelWidth);

      /* Current value */
      HardwareSensorsMeter_appendAlignedValue(
         &line, type, values[0], temperatureUnit,
         valueWidths[0], METER_VALUE);

      /* TUI equivalent of a one-character left border. */
      RichString_appendWide(&line, CRT_colors[METER_TEXT], " │ ");

      /* Minimum */
      RichString_appendAscii(&line, CRT_colors[METER_TEXT], "min ");
      HardwareSensorsMeter_appendAlignedValue(
         &line, type, values[1], temperatureUnit,
         valueWidths[1], METER_VALUE_OK);

      /* Average */
      RichString_appendAscii(&line, CRT_colors[METER_TEXT], "   avg ");
      HardwareSensorsMeter_appendAlignedValue(
         &line, type, values[2], temperatureUnit,
         valueWidths[2], METER_VALUE_NOTICE);

      /* Maximum */
      RichString_appendAscii(&line, CRT_colors[METER_TEXT], "   max ");
      HardwareSensorsMeter_appendAlignedValue(
         &line, type, values[3], temperatureUnit,
         valueWidths[3], METER_VALUE_WARN);

      int length = MINIMUM(w, RichString_sizeVal(line));

      if (length > 0)
         RichString_printoffnVal(line, y + (int)row, x, 0, length);

      RichString_delete(&line);
   }
#else
   attrset(CRT_colors[METER_VALUE_ERROR]);
   mvaddnstr(y, x, "Sensors: unavailable", w);
#endif

   attrset(CRT_colors[RESET_COLOR]);
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
   .updateMode = HardwareSensorsMeter_updateMode,
   .updateValues = HardwareSensorsMeter_updateValues,
   .draw = HardwareSensorsMeter_draw,
   .defaultMode = TEXT_METERMODE,
   .supportedModes = (1U << TEXT_METERMODE),
   .maxItems = 0,
   .isPercentChart = false,
   .total = 0.0,
   .attributes = HardwareSensorsMeter_attributes,
   .name = "HardwareSensors",
   .uiName = "Hardware sensors (multiline)",
   .caption = ""
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
