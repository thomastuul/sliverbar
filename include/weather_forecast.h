#ifndef SLIVERBAR_WEATHER_FORECAST_H
#define SLIVERBAR_WEATHER_FORECAST_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define WEATHER_FORECAST_DAY_COUNT 3
#define WEATHER_FORECAST_SLOT_COUNT 6

typedef enum {
  WEATHER_CONDITION_UNKNOWN,
  WEATHER_CONDITION_CLEAR,
  WEATHER_CONDITION_CLOUDY,
  WEATHER_CONDITION_RAIN,
  WEATHER_CONDITION_THUNDER,
  WEATHER_CONDITION_SNOW,
  WEATHER_CONDITION_FOG,
} WeatherCondition;

typedef struct {
  int hour;
  bool temperatureValid;
  int temperatureC;
  bool rainValid;
  int rainPercent;
  bool codeValid;
  int weatherCode;
  WeatherCondition condition;
} WeatherForecastSlot;

typedef struct {
  bool available;
  char date[11];
  bool minimumValid;
  int minimumC;
  bool maximumValid;
  int maximumC;
  WeatherForecastSlot slots[WEATHER_FORECAST_SLOT_COUNT];
} WeatherForecastDay;

typedef struct {
  size_t dayCount;
  bool updatedAtValid;
  time_t updatedAt;
  WeatherForecastDay days[WEATHER_FORECAST_DAY_COUNT];
} WeatherForecast;

int weatherForecastParse(const char *json, WeatherForecast *forecast);
WeatherCondition weatherConditionFromCode(int code);
const char *weatherConditionGlyph(WeatherCondition condition, bool useIconFont);
const char *weatherForecastDayName(const char *date,
                                   bool german,
                                   char *buffer,
                                   size_t size);
const char *weatherForecastUpdatedLabel(time_t updatedAt,
                                        bool valid,
                                        bool german,
                                        time_t now,
                                        char *buffer,
                                        size_t size);

#endif
