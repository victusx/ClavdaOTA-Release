#ifndef ClavdaWidget_h
#define ClavdaWidget_h

#include <Arduino.h>
#include <functional>
#include "../common/clavda_types.h"

/** Longest text a widget holds; longer text is cut. Keeps every frame small. */
#ifndef CLAVDA_WIDGET_MAX_TEXT
  #define CLAVDA_WIDGET_MAX_TEXT 256
#endif

/** Most bars a bar chart sends. */
#ifndef CLAVDA_WIDGET_MAX_SERIES
  #define CLAVDA_WIDGET_MAX_SERIES 32
#endif

/**
 * Recent line-chart points kept on the device, shared by every chart, so a
 * dashboard that opens late starts with some history. 12 bytes each.
 */
#ifndef CLAVDA_DASH_POINTS
  #define CLAVDA_DASH_POINTS 64
#endif

namespace clavda {
class Dash;
}

/** What a dashboard control sent: a toggle, slider, button or text box. */
class ClavdaInput {
  public:
    bool isBool() const { return _kind == BOOL; }
    bool isNumber() const { return _kind == NUMBER; }
    bool isText() const { return _kind == TEXT; }

    /** A toggle's state. Numbers count as on when not 0, text when "true", "on" or "1". */
    bool asBool() const;
    long asInt() const;
    float asFloat() const { return (float)asDouble(); }
    double asDouble() const;
    /** The text as typed, or a number or boolean written out. Valid inside onChange() only. */
    const char *asText() const;

  private:
    friend class clavda::Dash;
    enum Kind : uint8_t { NONE, BOOL, NUMBER, TEXT };
    Kind _kind = NONE;
    bool _bool = false;
    double _number = 0;
    const char *_text = "";
    mutable char _buf[24] = {0};
};

/**
 * One card or chart on this device's Clavda dashboard, named by the ID the
 * console gave it - copy the line from the widget's settings in the console.
 * The console decides what it looks like; the sketch sets its value, adds
 * chart points, and handles what its control sends.
 *
 *   ClavdaWidget temperature("temp_k3x9");
 *   ClavdaWidget relay("relay_m2p7");
 *
 *   relay.onChange([](const ClavdaInput &in) { digitalWrite(RELAY_PIN, in.asBool()); });
 *   temperature.set(23.4);
 *
 * Declare widgets at global scope, and use them from setup() and loop() rather
 * than from other tasks. A value goes to every open dashboard only when it
 * changes, so calling set() on every loop() costs nothing extra.
 */
class ClavdaWidget {
  public:
    using ChangeHandler = std::function<void(const ClavdaInput &input)>;

    explicit ClavdaWidget(const char *id);
    ~ClavdaWidget();
    ClavdaWidget(const ClavdaWidget &) = delete;
    ClavdaWidget &operator=(const ClavdaWidget &) = delete;

    /** Value, gauge, toggle and slider cards. */
    void set(double value) { setNumber(value, false); }
    void set(float value) { setNumber(value, true); }
    void set(int value) { setNumber(value, false); }
    void set(long value) { setNumber(value, false); }
    void set(unsigned int value) { setNumber(value, false); }
    void set(unsigned long value) { setNumber(value, false); }
    void set(long long value) { setNumber((double)value, false); }
    void set(unsigned long long value) { setNumber((double)value, false); }
    void set(bool value);
    /** Text, and status cards. */
    void set(const char *text) { setText(text, CLAVDA_NEUTRAL); }
    void set(const String &text) { setText(text.c_str(), CLAVDA_NEUTRAL); }
    /** A status card's message and colour. */
    void set(const char *text, ClavdaLevel level) { setText(text, level); }
    void set(const String &text, ClavdaLevel level) { setText(text.c_str(), level); }

    /** One point on a line chart. The console times it, so the board needs no clock. */
    void add(float value);

    /**
     * A bar chart's bars. The arrays stay the sketch's and are read when sent,
     * so keep them alive, and call this again after changing them.
     */
    void setSeries(const char *const labels[], const float values[], size_t count);
    void setSeries(const float values[], size_t count) { setSeries(nullptr, values, count); }

    /**
     * Called from ClavdaOTA.loop() when someone uses this widget's control. By
     * then the widget already holds the new value, and every open dashboard
     * shows it; set() something else here to override it.
     */
    void onChange(ChangeHandler handler) { _onChange = handler; }

    const char *id() const { return _id; }
    double number() const;
    bool isOn() const;
    const char *text() const { return _text.c_str(); }

  private:
    friend class clavda::Dash;
    enum Kind : uint8_t { NONE, NUMBER, BOOL, TEXT, SERIES };

    struct Point {
      ClavdaWidget *widget;
      float value;
      uint32_t at;  // millis()
    };

    void setNumber(double value, bool isFloat);
    void setText(const char *text, ClavdaLevel level);
    void changed() { _gen = ++_generation; }

    const char *_id;
    ClavdaWidget *_next = nullptr;
    Kind _kind = NONE;
    bool _float = false;
    bool _bool = false;
    ClavdaLevel _level = CLAVDA_NEUTRAL;
    double _number = 0;
    String _text;
    const char *const *_labels = nullptr;
    const float *_values = nullptr;
    size_t _count = 0;
    /** _generation when this widget last changed; 0 if it never has. */
    uint32_t _gen = 0;
    ChangeHandler _onChange;

    static ClavdaWidget *_head;
    /** Counts every change, so a dashboard can ask for "everything after N". */
    static uint32_t _generation;
    /** Bumped when a widget is added or removed, so dashboards resend everything. */
    static uint32_t _epoch;
    static Point *_points;
    /** Points ever added; the ring holds the last CLAVDA_DASH_POINTS of them. */
    static uint32_t _pointHead;
};

#endif
