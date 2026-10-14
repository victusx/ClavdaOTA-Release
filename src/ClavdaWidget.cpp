#include "ClavdaWidget.h"

#include <stdlib.h>
#include "esp_heap_caps.h"

ClavdaWidget *ClavdaWidget::_head = nullptr;
uint32_t ClavdaWidget::_generation = 0;
uint32_t ClavdaWidget::_epoch = 0;
ClavdaWidget::Point *ClavdaWidget::_points = nullptr;
uint32_t ClavdaWidget::_pointHead = 0;

bool ClavdaInput::asBool() const {
  switch (_kind) {
    case BOOL: return _bool;
    case NUMBER: return _number != 0;
    case TEXT: return !strcmp(_text, "true") || !strcmp(_text, "on") || !strcmp(_text, "1");
    default: return false;
  }
}

long ClavdaInput::asInt() const {
  const double value = asDouble();
  return (long)(value < 0 ? value - 0.5 : value + 0.5);
}

double ClavdaInput::asDouble() const {
  switch (_kind) {
    case BOOL: return _bool ? 1 : 0;
    case NUMBER: return _number;
    case TEXT: return atof(_text);
    default: return 0;
  }
}

const char *ClavdaInput::asText() const {
  switch (_kind) {
    case BOOL: return _bool ? "true" : "false";
    case NUMBER:
      snprintf(_buf, sizeof(_buf), "%g", _number);
      return _buf;
    case TEXT: return _text;
    default: return "";
  }
}

// Widgets are usually globals, constructed before setup(); the list only holds
// pointers, so construction order across files does not matter.
ClavdaWidget::ClavdaWidget(const char *id) : _id(id ? id : "") {
  ClavdaWidget **tail = &_head;
  while (*tail) tail = &(*tail)->_next;
  *tail = this;
  _epoch++;
}

ClavdaWidget::~ClavdaWidget() {
  for (ClavdaWidget **p = &_head; *p; p = &(*p)->_next) {
    if (*p == this) {
      *p = _next;
      break;
    }
  }
  if (_points) {
    for (size_t i = 0; i < CLAVDA_DASH_POINTS; i++) {
      if (_points[i].widget == this) _points[i].widget = nullptr;
    }
  }
  _epoch++;
}

void ClavdaWidget::setNumber(double value, bool isFloat) {
  if (_kind == NUMBER && _number == value && _float == isFloat) return;
  _kind = NUMBER;
  _number = value;
  _float = isFloat;
  changed();
}

void ClavdaWidget::set(bool value) {
  if (_kind == BOOL && _bool == value) return;
  _kind = BOOL;
  _bool = value;
  changed();
}

void ClavdaWidget::setText(const char *text, ClavdaLevel level) {
  if (!text) text = "";
  size_t len = strnlen(text, CLAVDA_WIDGET_MAX_TEXT + 1);
  if (len > CLAVDA_WIDGET_MAX_TEXT) {
    len = CLAVDA_WIDGET_MAX_TEXT;
    // Never cut a UTF-8 character in half.
    while (len && ((uint8_t)text[len] & 0xC0) == 0x80) len--;
  }
  if (_kind == TEXT && _level == level && _text.length() == len &&
      !strncmp(_text.c_str(), text, len)) {
    return;
  }
  _kind = TEXT;
  _level = level;
  _text = String();
  _text.concat(text, len);
  changed();
}

void ClavdaWidget::add(float value) {
  if (!_points) {
    // PSRAM when the board has it; a sketch without charts never pays for this.
    _points = (Point *)heap_caps_calloc_prefer(CLAVDA_DASH_POINTS, sizeof(Point), 2,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!_points) {
      log_e("[ClavdaOTA] No memory for chart points");
      return;
    }
  }
  _points[_pointHead % CLAVDA_DASH_POINTS] = {this, value, millis()};
  _pointHead++;
}

void ClavdaWidget::setSeries(const char *const labels[], const float values[], size_t count) {
  // Always a change: the sketch may have rewritten the arrays in place.
  _kind = SERIES;
  _labels = labels;
  _values = values;
  _count = values ? (count < CLAVDA_WIDGET_MAX_SERIES ? count : CLAVDA_WIDGET_MAX_SERIES) : 0;
  changed();
}

double ClavdaWidget::number() const {
  switch (_kind) {
    case NUMBER: return _number;
    case BOOL: return _bool ? 1 : 0;
    case TEXT: return atof(_text.c_str());
    default: return 0;
  }
}

bool ClavdaWidget::isOn() const {
  switch (_kind) {
    case BOOL: return _bool;
    case NUMBER: return _number != 0;
    default: return false;
  }
}
