#include "native_popup.h"

#include <cairo/cairo.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition);  \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (0)

static uint32_t pixel(const unsigned char *data, int stride, int x, int y) {
  uint32_t value = 0;
  memcpy(
      &value, data + (size_t)y * (size_t)stride + (size_t)x * 4, sizeof(value));
  return value & 0xffffffU;
}

static xcb_window_t popupWindow(xcb_connection_t *connection,
                                xcb_screen_t *screen,
                                int width,
                                int height) {
  xcb_query_tree_reply_t *tree = xcb_query_tree_reply(
      connection, xcb_query_tree(connection, screen->root), NULL);
  CHECK(tree != NULL);
  xcb_window_t result = XCB_NONE;
  xcb_window_t *children = xcb_query_tree_children(tree);
  for (int i = 0; i < xcb_query_tree_children_length(tree); i++) {
    xcb_get_geometry_reply_t *geometry = xcb_get_geometry_reply(
        connection, xcb_get_geometry(connection, children[i]), NULL);
    CHECK(geometry != NULL);
    if (geometry->width == width && geometry->height == height)
      result = children[i];
    free(geometry);
  }
  free(tree);
  CHECK(result != XCB_NONE);
  return result;
}

static void checkGrid(xcb_connection_t *connection,
                      xcb_window_t window,
                      int width,
                      int height,
                      const char *name) {
  xcb_generic_error_t *error = NULL;
  xcb_get_image_reply_t *image =
      xcb_get_image_reply(connection,
                          xcb_get_image(connection,
                                        XCB_IMAGE_FORMAT_Z_PIXMAP,
                                        window,
                                        0,
                                        0,
                                        (uint16_t)width,
                                        (uint16_t)height,
                                        UINT32_MAX),
                          &error);
  if (error) {
    fprintf(stderr,
            "%s: X11 image error %u for %dx%d window %u\n",
            name,
            error->error_code,
            width,
            height,
            window);
    free(error);
  }
  CHECK(image != NULL);
  unsigned char *data = xcb_get_image_data(image);
  int stride = xcb_get_image_data_length(image) / height;
  CHECK(stride == width * 4);

  const char *directory = getenv("SLIVERBAR_POPUP_CAPTURE_DIR");
  if (directory) {
    char path[4096];
    CHECK(snprintf(path, sizeof(path), "%s/%s.png", directory, name) > 0);
    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        data, CAIRO_FORMAT_RGB24, width, height, stride);
    CHECK(cairo_surface_write_to_png(surface, path) == CAIRO_STATUS_SUCCESS);
    cairo_surface_destroy(surface);
  }

  /* White grid strokes at 38% opacity on black must remain uninterrupted.
     Text or weather artwork touching a separator changes its pixels. */
  const uint32_t GRID = 0x616161U;
  int rows[32];
  size_t rowCount = 0;
  for (int y = 0; y < height; y++) {
    int matching = 0;
    for (int x = 1; x < width - 1; x++)
      if (pixel(data, stride, x, y) == GRID)
        matching++;
    if (matching < width * 3 / 4)
      continue;
    CHECK(rowCount < sizeof(rows) / sizeof(rows[0]));
    rows[rowCount++] = y;
    /* The separately stroked outer border overlaps vertical grid endpoints. */
    if (y == height - 1)
      continue;
    for (int x = 1; x < width - 1; x++) {
      if (pixel(data, stride, x, y) != GRID)
        fprintf(stderr,
                "%s: content touches horizontal grid at %d,%d\n",
                name,
                x,
                y);
      CHECK(pixel(data, stride, x, y) == GRID);
    }
  }
  CHECK(rowCount == 20);
  for (size_t day = 0; day < WEATHER_FORECAST_DAY_COUNT; day++) {
    int top = rows[2 + day * 6];
    int bottom = rows[7 + day * 6];
    size_t columns = 0;
    for (int x = 1; x < width - 1; x++) {
      if (pixel(data, stride, x, top + 1) != GRID)
        continue;
      columns++;
      for (int y = top + 1; y < bottom; y++) {
        if (pixel(data, stride, x, y) != GRID)
          fprintf(stderr,
                  "%s: content touches vertical grid at %d,%d\n",
                  name,
                  x,
                  y);
        CHECK(pixel(data, stride, x, y) == GRID);
      }
    }
    CHECK(columns == WEATHER_FORECAST_SLOT_COUNT);
  }
  free(image);
}

static void checkForecast(xcb_connection_t *connection,
                          xcb_screen_t *screen,
                          int fontSize,
                          const char *language,
                          int monitorWidth) {
  PanelConfig config;
  configDefaults(&config);
  snprintf(config.font, sizeof(config.font), "monospace:size=%d", fontSize);
  config.iconFont[0] = '\0';
  snprintf(config.language, sizeof(config.language), "%s", language);
  strcpy(config.colorBg, "#000000");
  strcpy(config.colorPanelBg, "#000000");
  strcpy(config.colorFree, "#ffffff");
  NativePopup *popup = nativePopupCreate(connection, screen, &config);
  CHECK(nativePopupAvailable(popup));
  nativePopupSetBounds(popup, 0, 0, monitorWidth, config.height);
  char json[32768];
  CHECK(readTextFile(SLIVERBAR_TEST_SOURCE_DIR
                     "/tests/fixtures/weather-forecast.json",
                     json,
                     sizeof(json)) == 0);
  WeatherForecast forecast;
  CHECK(weatherForecastParse(json, &forecast) == 0);
  forecast.updatedAtValid = true;
  forecast.updatedAt = time(NULL);
  CHECK(nativePopupOpenForecast(
            popup, &forecast, "München", monitorWidth - 120, 120) == 0);
  int x = 0, y = 0, width = 0, height = 0;
  nativePopupGeometry(popup, &x, &y, &width, &height);
  CHECK(x >= 0 && x + width <= monitorWidth);
  CHECK(y == config.height);
  CHECK(width >= 640 && height > 0);
  char name[128];
  snprintf(name,
           sizeof(name),
           "forecast-%s-%d-%d",
           language,
           fontSize,
           monitorWidth);
  checkGrid(connection,
            popupWindow(connection, screen, width, height),
            width,
            height,
            name);

  /* Updating an open forecast must recompute its layout as values change. */
  forecast.days[0].slots[0].windSpeedKmph = 500;
  forecast.days[0].slots[0].windSpeedValid = true;
  nativePopupUpdateForecast(popup, &forecast, "Uhldingen-Mühlhofen");
  nativePopupGeometry(popup, &x, &y, &width, &height);
  CHECK(x >= 0 && x + width <= monitorWidth);
  checkGrid(connection,
            popupWindow(connection, screen, width, height),
            width,
            height,
            "forecast-updated");
  WeatherForecast empty = {0};
  nativePopupUpdateForecast(popup, &empty, "München");
  int emptyHeight = 0;
  nativePopupGeometry(popup, NULL, NULL, NULL, &emptyHeight);
  CHECK(emptyHeight > 0 && emptyHeight < height);
  nativePopupDestroy(popup);
  printf("%s: grid clear of text and weather artwork (%dx%d)\n",
         name,
         width,
         height);
}

int main(void) {
  xcb_connection_t *connection = xcb_connect(NULL, NULL);
  CHECK(!xcb_connection_has_error(connection));
  xcb_screen_t *screen =
      xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
  const int SIZES[] = {10, 15, 17, 24};
  for (size_t i = 0; i < sizeof(SIZES) / sizeof(SIZES[0]); i++) {
    checkForecast(connection, screen, SIZES[i], "de", 1600);
    checkForecast(connection, screen, SIZES[i], "en", 1600);
  }
  checkForecast(connection, screen, 24, "de", 640);
  checkForecast(connection, screen, 24, "en", 640);
  xcb_disconnect(connection);
  return EXIT_SUCCESS;
}
