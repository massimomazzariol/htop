/*
htop - HardwareSensor.c
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h" // IWYU pragma: keep

#include "HardwareSensor.h"

#include <string.h>

#include "XUtils.h"


void HardwareSensor_formatName(char* buffer, size_t size,
                               const char* chip,
                               const char* label,
                               const char* feature,
                               HardwareSensorType type) {
   if (label && label[0]) {
      xSnprintf(buffer, size, "%s", label);
      return;
   }

   if (type == SENSOR_FAN && feature &&
       String_startsWith(feature, "fan") && feature[3]) {
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

   if (len > thermalSuffixLen &&
       String_eq(buffer + len - thermalSuffixLen, thermalSuffix))
      buffer[len - thermalSuffixLen] = '\0';

   for (char* token = buffer; *token;) {
      while (*token == ' ')
         token++;

      if (!*token)
         break;

      char* end = token;
      while (*end && *end != ' ')
         end++;

      /* Short tokens are typically hardware abbreviations such as CPU, GPU, or PCH. */
      if ((size_t)(end - token) <= 3) {
         for (char* p = token; p < end; p++) {
            if (*p >= 'a' && *p <= 'z')
               *p -= 'a' - 'A';
         }
      }

      token = end;
   }
}
