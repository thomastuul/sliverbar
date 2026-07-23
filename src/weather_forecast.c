#include "weather_forecast.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *begin;
  const char *end;
} JsonValue;

static const int FORECAST_HOURS[WEATHER_FORECAST_SLOT_COUNT] = {
    6, 9, 12, 15, 18, 21};

static const char *skipWhitespace(const char *cursor, const char *end) {
  while (cursor < end && isspace((unsigned char)*cursor))
    cursor++;
  return cursor;
}

static const char *skipString(const char *cursor, const char *end) {
  if (cursor >= end || *cursor != '"')
    return NULL;
  cursor++;
  while (cursor < end) {
    if (*cursor == '"')
      return cursor + 1;
    if (*cursor == '\\') {
      cursor++;
      if (cursor >= end)
        return NULL;
    }
    cursor++;
  }
  return NULL;
}

static const char *
skipValue(const char *cursor, const char *end, unsigned depth) {
  cursor = skipWhitespace(cursor, end);
  if (cursor >= end || depth > 64)
    return NULL;
  if (*cursor == '"')
    return skipString(cursor, end);
  if (*cursor == '{' || *cursor == '[') {
    char close = *cursor == '{' ? '}' : ']';
    cursor++;
    cursor = skipWhitespace(cursor, end);
    if (cursor < end && *cursor == close)
      return cursor + 1;
    while (cursor < end) {
      if (close == '}') {
        cursor = skipString(cursor, end);
        if (!cursor)
          return NULL;
        cursor = skipWhitespace(cursor, end);
        if (cursor >= end || *cursor != ':')
          return NULL;
        cursor++;
      }
      cursor = skipValue(cursor, end, depth + 1);
      if (!cursor)
        return NULL;
      cursor = skipWhitespace(cursor, end);
      if (cursor >= end)
        return NULL;
      if (*cursor == close)
        return cursor + 1;
      if (*cursor != ',')
        return NULL;
      cursor = skipWhitespace(cursor + 1, end);
    }
    return NULL;
  }
  const char *start = cursor;
  while (cursor < end && *cursor != ',' && *cursor != '}' && *cursor != ']')
    cursor++;
  const char *trimmed = cursor;
  while (trimmed > start && isspace((unsigned char)trimmed[-1]))
    trimmed--;
  return trimmed > start ? cursor : NULL;
}

static bool keyMatches(const char *begin, const char *end, const char *key) {
  if (!begin || !end || begin >= end || *begin != '"' || end[-1] != '"')
    return false;
  begin++;
  end--;
  size_t length = (size_t)(end - begin);
  return length == strlen(key) && !memcmp(begin, key, length);
}

static bool objectMember(JsonValue object, const char *key, JsonValue *value) {
  const char *cursor = skipWhitespace(object.begin, object.end);
  if (cursor >= object.end || *cursor != '{')
    return false;
  cursor = skipWhitespace(cursor + 1, object.end);
  while (cursor < object.end && *cursor != '}') {
    const char *keyBegin = cursor;
    const char *keyEnd = skipString(cursor, object.end);
    if (!keyEnd)
      return false;
    cursor = skipWhitespace(keyEnd, object.end);
    if (cursor >= object.end || *cursor != ':')
      return false;
    const char *valueBegin = skipWhitespace(cursor + 1, object.end);
    const char *valueEnd = skipValue(valueBegin, object.end, 0);
    if (!valueEnd)
      return false;
    if (keyMatches(keyBegin, keyEnd, key)) {
      value->begin = valueBegin;
      value->end = valueEnd;
      return true;
    }
    cursor = skipWhitespace(valueEnd, object.end);
    if (cursor >= object.end || *cursor == '}')
      break;
    if (*cursor != ',')
      return false;
    cursor = skipWhitespace(cursor + 1, object.end);
  }
  return false;
}

static bool arrayElement(JsonValue array, size_t requested, JsonValue *value) {
  const char *cursor = skipWhitespace(array.begin, array.end);
  if (cursor >= array.end || *cursor != '[')
    return false;
  cursor = skipWhitespace(cursor + 1, array.end);
  size_t index = 0;
  while (cursor < array.end && *cursor != ']') {
    const char *valueEnd = skipValue(cursor, array.end, 0);
    if (!valueEnd)
      return false;
    if (index == requested) {
      value->begin = cursor;
      value->end = valueEnd;
      return true;
    }
    index++;
    cursor = skipWhitespace(valueEnd, array.end);
    if (cursor >= array.end || *cursor == ']')
      break;
    if (*cursor != ',')
      return false;
    cursor = skipWhitespace(cursor + 1, array.end);
  }
  return false;
}

static bool valueText(JsonValue value, char *output, size_t size) {
  const char *begin = skipWhitespace(value.begin, value.end);
  if (!output || size == 0 || begin >= value.end || *begin != '"')
    return false;
  const char *end = skipString(begin, value.end);
  if (!end || end != value.end)
    return false;
  begin++;
  end--;
  size_t length = (size_t)(end - begin);
  if (length >= size || memchr(begin, '\\', length))
    return false;
  memcpy(output, begin, length);
  output[length] = '\0';
  return true;
}

static bool valueInteger(JsonValue value, int *output) {
  char text[32];
  const char *begin = skipWhitespace(value.begin, value.end);
  const char *end = value.end;
  while (end > begin && isspace((unsigned char)end[-1]))
    end--;
  if (begin < end && *begin == '"') {
    if (!valueText(value, text, sizeof(text)))
      return false;
  } else {
    size_t length = (size_t)(end - begin);
    if (length == 0 || length >= sizeof(text))
      return false;
    memcpy(text, begin, length);
    text[length] = '\0';
  }
  char *parseEnd = NULL;
  errno = 0;
  long number = strtol(text, &parseEnd, 10);
  if (errno || parseEnd == text || *parseEnd || number < INT_MIN ||
      number > INT_MAX)
    return false;
  *output = (int)number;
  return true;
}

static bool memberInteger(JsonValue object, const char *key, int *output) {
  JsonValue value;
  return objectMember(object, key, &value) && valueInteger(value, output);
}

WeatherCondition weatherConditionFromCode(int code) {
  switch (code) {
  case 113:
    return WEATHER_CONDITION_CLEAR;
  case 116:
  case 119:
  case 122:
    return WEATHER_CONDITION_CLOUDY;
  case 143:
  case 248:
  case 260:
    return WEATHER_CONDITION_FOG;
  case 200:
  case 386:
  case 389:
  case 392:
  case 395:
    return WEATHER_CONDITION_THUNDER;
  case 179:
  case 182:
  case 185:
  case 227:
  case 230:
  case 317:
  case 320:
  case 323:
  case 326:
  case 329:
  case 332:
  case 335:
  case 338:
  case 368:
  case 371:
  case 374:
  case 377:
    return WEATHER_CONDITION_SNOW;
  case 176:
  case 263:
  case 266:
  case 281:
  case 284:
  case 293:
  case 296:
  case 299:
  case 302:
  case 305:
  case 308:
  case 311:
  case 314:
  case 353:
  case 356:
  case 359:
  case 362:
  case 365:
    return WEATHER_CONDITION_RAIN;
  default:
    return WEATHER_CONDITION_UNKNOWN;
  }
}

const char *weatherConditionGlyph(WeatherCondition condition,
                                  bool useIconFont) {
  static const char *const ICON_GLYPHS[] = {
      "?", "", "", "", "", "", ""};
  static const char *const UNICODE_GLYPHS[] = {
      "?", "☀", "☁", "☂", "⚡", "❄", "≋"};
  if (condition < WEATHER_CONDITION_UNKNOWN ||
      condition > WEATHER_CONDITION_FOG)
    condition = WEATHER_CONDITION_UNKNOWN;
  return useIconFont ? ICON_GLYPHS[condition] : UNICODE_GLYPHS[condition];
}

static bool validDate(int year, int month, int day) {
  static const int DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (year < 1 || month < 1 || month > 12 || day < 1)
    return false;
  int maximum = DAYS[month - 1];
  bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  if (month == 2 && leap)
    maximum++;
  return day <= maximum;
}

const char *weatherForecastDayName(const char *date,
                                   bool german,
                                   char *buffer,
                                   size_t size) {
  static const char *const GERMAN[] = {"Sonntag",
                                       "Montag",
                                       "Dienstag",
                                       "Mittwoch",
                                       "Donnerstag",
                                       "Freitag",
                                       "Samstag"};
  static const char *const ENGLISH[] = {"Sunday",
                                        "Monday",
                                        "Tuesday",
                                        "Wednesday",
                                        "Thursday",
                                        "Friday",
                                        "Saturday"};
  int year = 0, month = 0, day = 0;
  char extra = '\0';
  if (!date || sscanf(date, "%d-%d-%d%c", &year, &month, &day, &extra) != 3 ||
      !validDate(year, month, day)) {
    snprintf(buffer, size, "%s", "-");
    return buffer;
  }
  static const int OFFSETS[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int adjustedYear = month < 3 ? year - 1 : year;
  int weekday = (adjustedYear + adjustedYear / 4 - adjustedYear / 100 +
                 adjustedYear / 400 + OFFSETS[month - 1] + day) %
                7;
  snprintf(buffer, size, "%s", german ? GERMAN[weekday] : ENGLISH[weekday]);
  return buffer;
}

static void initializeForecast(WeatherForecast *forecast) {
  memset(forecast, 0, sizeof(*forecast));
  for (size_t day = 0; day < WEATHER_FORECAST_DAY_COUNT; day++)
    for (size_t slot = 0; slot < WEATHER_FORECAST_SLOT_COUNT; slot++)
      forecast->days[day].slots[slot].hour = FORECAST_HOURS[slot];
}

static void parseSlot(JsonValue hourly, WeatherForecastDay *day) {
  int time = 0;
  if (!memberInteger(hourly, "time", &time))
    return;
  size_t selected = WEATHER_FORECAST_SLOT_COUNT;
  for (size_t i = 0; i < WEATHER_FORECAST_SLOT_COUNT; i++)
    if (time == FORECAST_HOURS[i] * 100) {
      selected = i;
      break;
    }
  if (selected == WEATHER_FORECAST_SLOT_COUNT)
    return;
  WeatherForecastSlot *slot = &day->slots[selected];
  int value = 0;
  if (memberInteger(hourly, "tempC", &value) && value >= -100 && value <= 100) {
    slot->temperatureValid = true;
    slot->temperatureC = value;
  }
  if (memberInteger(hourly, "chanceofrain", &value) && value >= 0 &&
      value <= 100) {
    slot->rainValid = true;
    slot->rainPercent = value;
  }
  if (memberInteger(hourly, "weatherCode", &value) && value >= 0) {
    slot->codeValid = true;
    slot->weatherCode = value;
    slot->condition = weatherConditionFromCode(value);
  }
}

static bool parseDay(JsonValue object, WeatherForecastDay *day) {
  JsonValue value;
  bool available = false;
  if (objectMember(object, "date", &value) &&
      valueText(value, day->date, sizeof(day->date)))
    available = true;
  int number = 0;
  if (memberInteger(object, "mintempC", &number) && number >= -100 &&
      number <= 100) {
    day->minimumValid = true;
    day->minimumC = number;
    available = true;
  }
  if (memberInteger(object, "maxtempC", &number) && number >= -100 &&
      number <= 100) {
    day->maximumValid = true;
    day->maximumC = number;
    available = true;
  }
  JsonValue hourly;
  if (objectMember(object, "hourly", &hourly)) {
    for (size_t index = 0;; index++) {
      JsonValue item;
      if (!arrayElement(hourly, index, &item))
        break;
      parseSlot(item, day);
    }
  }
  for (size_t i = 0; i < WEATHER_FORECAST_SLOT_COUNT; i++) {
    const WeatherForecastSlot *slot = &day->slots[i];
    if (slot->temperatureValid || slot->rainValid || slot->codeValid)
      available = true;
  }
  day->available = available;
  return available;
}

int weatherForecastParse(const char *json, WeatherForecast *forecast) {
  if (!json || !forecast)
    return -1;
  initializeForecast(forecast);
  JsonValue root = {.begin = json, .end = json + strlen(json)};
  const char *rootEnd = skipValue(root.begin, root.end, 0);
  if (!rootEnd || skipWhitespace(rootEnd, root.end) != root.end)
    return -1;
  JsonValue days;
  if (!objectMember(root, "weather", &days))
    return -1;
  for (size_t index = 0; index < WEATHER_FORECAST_DAY_COUNT; index++) {
    JsonValue object;
    if (!arrayElement(days, index, &object))
      break;
    if (parseDay(object, &forecast->days[index]))
      forecast->dayCount = index + 1;
  }
  return forecast->dayCount > 0 ? 0 : -1;
}
