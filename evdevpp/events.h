#ifndef EVDEVPP_EVDEVPP_EVENTS_H_
#define EVDEVPP_EVDEVPP_EVENTS_H_

#include <cassert>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <utility>

#include "absl/time/time.h"
#include "evdevpp/ecodes.h"
#include "fmt/chrono.h"
#include "fmt/format.h"

namespace evdevpp {

// This library provides the `InputEvent` class, which mirrors
// the `input_event` struct defined in `linux/input.h`:
//
// struct input_event {
//   struct timeval time;
//   __u16 type;
//   __u16 code;
//   __s32 value;
// };
//
// This module also defines several `InputEvent` sub-classes that
// know more about the different types of events (key, abs, rel, etc).
//
// The `AnyInputEvent` class can be used to store by value a categorized
// event (polymorphic). Categorizing events can be done with the
// `AnyInputEvent::Categorize` function. Note, however, that this could be
// an expensive operation involving hash-map lookups, and it's usually not
// necessary, but can produce nicer string formatting and such.
//
// All classes in this library have reasonable `fmt` formatting capability.

// A generic input event.
struct InputEvent {
  absl::Time timestamp = absl::InfinitePast();
  EventType type{};
  std::uint16_t code = 0;
  std::int32_t value = 0;

  InputEvent() = default;
  InputEvent(absl::Time init_timestamp, EventType init_type,
             std::uint16_t init_code, std::int32_t init_value)
      : timestamp(init_timestamp),
        type(init_type),
        code(init_code),
        value(init_value) {}
  InputEvent(const InputEvent&) = default;
  InputEvent& operator=(const InputEvent&) = default;
  InputEvent(InputEvent&&) = default;
  InputEvent& operator=(InputEvent&&) = default;
  virtual ~InputEvent() = default;

  [[nodiscard]] virtual const char* TypeAsString() const {
    return type.ToString();
  }
  [[nodiscard]] virtual bool IsInCategory() const { return false; };
  [[nodiscard]] virtual const char* CodeAsString() const {
    return "UNCATEGORIZED";
  }

  static constexpr std::uint16_t kUncategorizedCode =
      std::numeric_limits<std::uint16_t>::max();
  // Internal use.
  virtual void CloneInplace(InputEvent* ptr) const noexcept {
    new (ptr) InputEvent(*this);
  }
};

// An event generated by a keyboard, button or other key-like devices.
struct KeyEvent : InputEvent {
  enum class State {
    kUp,
    kDown,
    kHold,
  };
  State state = State::kUp;
  Key key = kUncategorizedCode;
  Button button = kUncategorizedCode;

  static State ToState(std::int32_t v) {
    switch (v) {
      case 1:
        return State::kDown;
      case 2:
        return State::kHold;
      default:
        return State::kUp;
    }
  }

  template <typename... Args>
  explicit KeyEvent(Args&&... args)
      : InputEvent(std::forward<Args>(args)...), state(ToState(value)) {
    const auto& keys = Key::CodeToString();
    if (keys.find(code) != keys.end()) {
      key = code;
    }
    const auto& buttons = Button::CodeToString();
    if (buttons.find(code) != buttons.end()) {
      button = code;
    }
  }

  [[nodiscard]] bool IsKey() const { return (key != kUncategorizedCode); }
  [[nodiscard]] bool IsButton() const { return (button != kUncategorizedCode); }
  [[nodiscard]] bool IsInCategory() const override {
    return IsKey() || IsButton();
  }

  [[nodiscard]] const char* TypeAsString() const override {
    return Key::kClassName;
  }
  [[nodiscard]] const char* CodeAsString() const override {
    if (IsKey()) {
      return key.ToString();
    }
    if (IsButton()) {
      return button.ToString();
    }
    return "UNKNOWN";
  }
  // For internal use.
  using ExpectedECodeType = Key;
  void CloneInplace(InputEvent* ptr) const noexcept override {
    new (ptr) KeyEvent(*this);
  }
};

template <typename ECodeType>
struct CategorizedEvent : InputEvent {
  ECodeType categorized = kUncategorizedCode;

  template <typename... Args>
  explicit CategorizedEvent(Args&&... args)
      : InputEvent(std::forward<Args>(args)...) {
    const auto& codes = ECodeType::CodeToString();
    if (codes.find(code) != codes.end()) {
      categorized = code;
    }
  }

  [[nodiscard]] bool IsInCategory() const override {
    return (categorized != kUncategorizedCode);
  }

  [[nodiscard]] const char* TypeAsString() const override {
    return ECodeType::kClassName;
  }
  [[nodiscard]] const char* CodeAsString() const override {
    return categorized.ToString();
  }
  // For internal use.
  using ExpectedECodeType = ECodeType;
  void CloneInplace(InputEvent* ptr) const noexcept override {
    new (ptr) CategorizedEvent<ECodeType>(*this);
  }
};

// A relative axis event (e.g moving the mouse 5 units to the left).
using RelEvent = CategorizedEvent<RelativeAxis>;

// A absolute axis event (e.g moving the mouse 5 units to the left).
using AbsEvent = CategorizedEvent<AbsoluteAxis>;

// A synchronization event. Used as markers to separate events. Events may be
// separated in time or in space, such as with the multitouch protocol.
using SynchEvent = CategorizedEvent<Synch>;

// A switch event (e.g., plugging in headphones).
using SwitchEvent = CategorizedEvent<Switch>;

// A miscellaneous event (e.g., gesture).
using MiscEvent = CategorizedEvent<Misc>;

// An LED event (e.g., caps-lock).
using LEDEvent = CategorizedEvent<LED>;

// An auto-repeat event (e.g., pulsing).
using AutorepeatEvent = CategorizedEvent<Autorepeat>;

// A sound event (e.g., click).
using SoundEvent = CategorizedEvent<Sound>;

// A force feedback event (e.g., playing status).
using ForceFeedbackEvent = CategorizedEvent<ForceFeedback>;

// A user-input force feedback event (e.g., upload effect).
using UIForceFeedbackEvent = CategorizedEvent<UIForceFeedback>;

template <typename ActualEvent>
EventType ExpectedEventType() {
  if constexpr (std::is_same_v<ActualEvent, KeyEvent>) {
    return EventType::kKey;
  } else if constexpr (std::is_same_v<ActualEvent, SynchEvent>) {
    return EventType::kSyn;
  } else if constexpr (std::is_same_v<ActualEvent, RelEvent>) {
    return EventType::kRel;
  } else if constexpr (std::is_same_v<ActualEvent, AbsEvent>) {
    return EventType::kAbs;
  } else if constexpr (std::is_same_v<ActualEvent, MiscEvent>) {
    return EventType::kMsc;
  } else if constexpr (std::is_same_v<ActualEvent, SwitchEvent>) {
    return EventType::kSw;
  } else if constexpr (std::is_same_v<ActualEvent, LEDEvent>) {
    return EventType::kLed;
  } else if constexpr (std::is_same_v<ActualEvent, SoundEvent>) {
    return EventType::kSnd;
  } else if constexpr (std::is_same_v<ActualEvent, AutorepeatEvent>) {
    return EventType::kRep;
  } else if constexpr (std::is_same_v<ActualEvent, ForceFeedbackEvent>) {
    return EventType::kFfStatus;
  } else if constexpr (std::is_same_v<ActualEvent, UIForceFeedbackEvent>) {
    return EventType::kUinput;
  } else {
    return EventType::kMax;
  }
};

class AnyInputEvent {
 public:
  explicit AnyInputEvent(const InputEvent& rhs) : data_{.base = rhs} {
    Base().~InputEvent();
    rhs.CloneInplace(&Base());
  }
  AnyInputEvent() : AnyInputEvent(InputEvent{}) {}
  AnyInputEvent(const AnyInputEvent& rhs) : AnyInputEvent(rhs.Base()) {}
  AnyInputEvent& operator=(const AnyInputEvent& rhs) noexcept {
    if (this == &rhs) {
      return *this;
    }
    Base().~InputEvent();
    rhs.Base().CloneInplace(&Base());
    return *this;
  }
  AnyInputEvent(AnyInputEvent&& rhs) noexcept : AnyInputEvent(rhs.Base()) {}
  AnyInputEvent& operator=(AnyInputEvent&& rhs) noexcept {
    return *this = static_cast<const AnyInputEvent&>(rhs);
  }
  ~AnyInputEvent() = default;

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  [[nodiscard]] InputEvent& Base() { return data_.base; }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  [[nodiscard]] const InputEvent& Base() const { return data_.base; }

  // Casting functions to get the derived-class event.
  template <typename ActualEvent>
  [[nodiscard]] ActualEvent& As() {
    assert(ExpectedEventType<ActualEvent>() == Base().type);
    assert(Base().IsInCategory());
    return static_cast<ActualEvent&>(Base());
  }
  template <typename ActualEvent>
  [[nodiscard]] const ActualEvent& As() const {
    assert(ExpectedEventType<ActualEvent>() == Base().type);
    assert(Base().IsInCategory());
    return static_cast<const ActualEvent&>(Base());
  }

  // Categorize an event according to its type.
  //
  // If the event cannot be categorized, it is copied uncategorized.
  static AnyInputEvent Categorize(const InputEvent& uncategorized_ev);

  void Categorize() { *this = Categorize(Base()); }

 private:
  union InputEventUnion {
    InputEvent base;
    KeyEvent key_event;
    RelEvent rel_event;
    AbsEvent abs_event;
    SynchEvent synch_event;
    SwitchEvent switch_event;
    MiscEvent misc_event;
    LEDEvent led_event;
    AutorepeatEvent autorepeat_event;
    SoundEvent sound_event;
    ForceFeedbackEvent ff_event;
    UIForceFeedbackEvent ui_ff_event;

    InputEventUnion(const InputEventUnion&) = delete;
    InputEventUnion(InputEventUnion&&) = delete;
    InputEventUnion& operator=(const InputEventUnion&) = delete;
    InputEventUnion& operator=(InputEventUnion&&) = delete;

    ~InputEventUnion() { base.~InputEvent(); }
  } data_;
};

// Just a wrapper to make a formattable type out of a absl::Time timestamp.
struct InputEventTimestampFormatWrapper {
  const absl::Time* timestamp;
};

}  // namespace evdevpp

template <>
class fmt::formatter<evdevpp::InputEventTimestampFormatWrapper> {
 public:
  // NOLINTNEXTLINE(readability-identifier-naming) Following API.
  auto parse(format_parse_context& ctx) {
    const auto* begin = ctx.begin();
    const auto* end = ctx.end();
    if (begin == end) {
      return begin;
    }
    const auto* end_brace = std::find(begin, end, '}');
    if (end_brace == end) {
      throw fmt::format_error("Invalid format - No matching brace");
    }
    return end_brace;
  }

  template <typename FmtContext>
  // NOLINTNEXTLINE(readability-identifier-naming) Following API.
  auto format(const evdevpp::InputEventTimestampFormatWrapper& t,
              FmtContext& ctx) const {
    // This implementation allows for printing without allocating memory.
    std::tm t_tm = absl::ToTM(*t.timestamp, absl::UTCTimeZone());
    return fmt::format_to(
        ctx.out(), "{:%FT%T}.{:09d}Z", t_tm,
        absl::ToInt64Nanoseconds(*t.timestamp -
                                 absl::FromTM(t_tm, absl::UTCTimeZone())));
  }
};

template <>
struct fmt::formatter<evdevpp::InputEvent> {
  // NOLINTNEXTLINE(readability-identifier-naming) Following API.
  auto parse(format_parse_context& ctx) {
    const auto* begin = ctx.begin();
    const auto* end = ctx.end();
    if (begin == end) {
      return begin;
    }
    const auto* end_brace = std::find(begin, end, '}');
    if (end_brace == end) {
      throw fmt::format_error("Invalid format - No matching brace");
    }
    return end_brace;
  }

  template <typename FmtContext>
  // NOLINTNEXTLINE(readability-identifier-naming) Following API.
  auto format(const evdevpp::InputEvent& ev, FmtContext& ctx) const {
    auto ctx_out = fmt::format_to(
        ctx.out(), "{:<14s} event at {}, ", ev.TypeAsString(),
        evdevpp::InputEventTimestampFormatWrapper{&ev.timestamp});
    ctx_out = fmt::format_to(ctx_out, "{:<20s} (0x{:04X}), ", ev.CodeAsString(),
                             ev.code);
    return fmt::format_to(ctx_out, "value: {:12d}", ev.value);
  }
};

template <>
struct fmt::formatter<evdevpp::KeyEvent> : fmt::formatter<evdevpp::InputEvent> {
  template <typename FmtContext>
  // NOLINTNEXTLINE(readability-identifier-naming) Following API.
  auto format(const evdevpp::KeyEvent& ev, FmtContext& ctx) const {
    auto ctx_out = fmt::format_to(
        ctx.out(), "{} event at {}, ", ev.TypeAsString(),
        evdevpp::InputEventTimestampFormatWrapper{&ev.timestamp});
    ctx_out =
        fmt::format_to(ctx_out, "{} (0x{:04X}), ", ev.CodeAsString(), ev.code);
    switch (ev.state) {
      case evdevpp::KeyEvent::State::kUp:
        return fmt::format_to(ctx_out, "{}", "up");
      case evdevpp::KeyEvent::State::kDown:
        return fmt::format_to(ctx_out, "{}", "down");
      case evdevpp::KeyEvent::State::kHold:
        return fmt::format_to(ctx_out, "{}", "hold");
    }
  }
};

template <>
struct fmt::formatter<evdevpp::AnyInputEvent>
    : fmt::formatter<evdevpp::InputEvent> {
  template <typename FmtContext>
  // NOLINTNEXTLINE(readability-identifier-naming) Following API.
  auto format(const evdevpp::AnyInputEvent& ev, FmtContext& ctx) const {
    return fmt::formatter<evdevpp::InputEvent>::format(ev.Base(), ctx);
  }
};

#endif  // EVDEVPP_EVDEVPP_EVENTS_H_
