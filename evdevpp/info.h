#ifndef EVDEVPP_EVDEVPP_INFO_H_
#define EVDEVPP_EVDEVPP_INFO_H_

#include <array>
#include <cassert>
#include <cstdint>
#include <new>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/time/time.h"
#include "evdevpp/ecodes.h"
#include "fmt/format.h"

namespace evdevpp {

// Absolute axis information.
//
// A `namedtuple` used for storing absolute axis information -
// corresponds to the `input_absinfo` struct:
//
// Attributes
// ---------
// `value`: Latest reported value for the axis.
// `min`: Specifies minimum value for the axis.
// `max`: Specifies maximum value for the axis.
// `fuzz`: Specifies fuzz value that is used to filter noise from the
//         event stream.
// `flat`: Values that are within this value will be discarded by joydev
//         interface and reported as 0 instead.
// `resolution`: Specifies resolution for the values reported for the axis.
//               Resolution for main axes (`ABS_X, ABS_Y, ABS_Z`) is reported
//               in units per millimeter (units/mm), resolution for rotational
//               axes (`ABS_RX, ABS_RY, ABS_RZ`) is reported in units per
//               radian.
// Note
// ----
// The input core does not clamp reported values to the `[minimum, maximum]`
// limits, such task is left to userspace.
struct AbsInfo {
  std::int32_t value = 0;
  std::int32_t minimum = 0;
  std::int32_t maximum = 0;
  std::int32_t fuzz = 0;
  std::int32_t flat = 0;
  std::int32_t resolution = 0;
};

// Capabilities is a mapping of supported event types to lists of handled
// events e.g: {keys: [272, 273, 274, 275], synchs: [0, 1, 6, 8]...}
struct CapabilitiesInfo {
  absl::flat_hash_set<std::uint16_t> keys;                    // Key or Button
  absl::flat_hash_set<std::uint16_t> synchs;                  // Synch
  absl::flat_hash_set<std::uint16_t> relative_axes;           // RelativeAxis
  absl::flat_hash_map<std::uint16_t, AbsInfo> absolute_axes;  // AbsoluteAxis
  absl::flat_hash_set<std::uint16_t> miscs;                   // Misc
  absl::flat_hash_set<std::uint16_t> switches;                // Switch
  absl::flat_hash_set<std::uint16_t> leds;                    // LED
  absl::flat_hash_set<std::uint16_t> sounds;                  // Sound
  absl::flat_hash_set<std::uint16_t> autorepeats;             // Autorepeat
  absl::flat_hash_set<std::uint16_t> force_feedbacks;         // ForceFeedback
  absl::flat_hash_set<std::uint16_t> uinputs;                 // UIForceFeedback

  static CapabilitiesInfo AllKeys();

  [[nodiscard]] bool HasCapabilities(const CapabilitiesInfo& min_caps) const;
};

// Keyboard repeat rate.
//
// Attributes
// ----------
// `repeat`: Keyboard repeat rate in characters per second.
// `delay`: Amount of time that a key must be depressed before it will start
//          to repeat (in milliseconds).
struct KeyRepeatInfo {
  std::uint32_t repeat_key_per_s = 0;
  absl::Duration delay{};
};

// Device information.
struct DeviceInfo {
  std::uint16_t bustype = 0;
  std::uint16_t vendor = 0;
  std::uint16_t product = 0;
  std::uint16_t version = 0;
};

// Defines scheduling of the force-feedback effect
// `length`: duration of the effect
// `delay`: delay before effect should start playing
struct Replay {
  absl::Duration length{};
  absl::Duration delay{};
};

// Defines what triggers the force-feedback effect
// `button`: number of the button triggering the effect
// `interval`: controls how soon the effect can be re-triggered
struct Trigger {
  std::uint16_t button = 0;
  absl::Duration interval{};
};

// Defines force feedback effect
// `id`: an unique id assigned to an effect
// `direction`: direction of the effect
// `trigger`: trigger conditions (struct ff_trigger)
// `replay`: scheduling of the effect (struct ff_replay)
//
// Derived effect-specific structures (one of `ConstantEffect`, `RampEffect`,
// `PeriodicEffect`, `ConditionEffect`, `RumbleEffect`) further defining
// effect parameters
//
// This structure is sent through ioctl from the application to the driver.
// To create a new effect application should set its @id to -1; the kernel
// will return assigned @id which can later be used to update or delete
// this effect.
//
// Direction of the effect is encoded as follows:
//  0 deg -> 0x0000 (down)
//  90 deg -> 0x4000 (left)
//  180 deg -> 0x8000 (up)
//  270 deg -> 0xC000 (right)
struct Effect {
  std::int16_t id = 0;
  std::uint16_t direction = 0;
  Trigger trigger{};
  Replay replay{};

  Effect() = default;
  Effect(const Effect&) = default;
  Effect& operator=(const Effect&) = default;
  Effect(Effect&&) noexcept = default;
  Effect& operator=(Effect&&) noexcept = default;
  virtual ~Effect() = default;

  // Type of the effect:
  //  ForceFeedback::kConstant, ForceFeedback::kPeriodic, ForceFeedback::kRamp,
  //  ForceFeedback::kSpring, ForceFeedback::kFriction, ForceFeedback::kDamper,
  //  ForceFeedback::kRumble, ForceFeedback::kInertia, or
  //  ForceFeedback::kCustom.
  [[nodiscard]] virtual ForceFeedback Type() const {
    return ForceFeedback::kMax;
  }

  virtual void ToData(void* data_ptr) const;
  virtual void FromData(const void* data_ptr);

  // Internal use.
  virtual void CloneInplace(Effect* ptr) const noexcept {
    new (ptr) Effect(*this);
  }
};

// Generic force-feedback effect envelope
// `attack_length`: duration of the attack (ms)
// `attack_level`: level at the beginning of the attack
// `fade_length`: duration of fade (ms)
// `fade_level`: level at the end of fade
//
// The `attack_level` and `fade_level` are absolute values; when applying
// envelope force-feedback core will convert to positive/negative
// value based on polarity of the default level of the effect.
// Valid range for the attack and fade levels is 0x0000 - 0x7fff
struct Envelope {
  absl::Duration attack_length{};
  std::uint16_t attack_level = 0;
  absl::Duration fade_length{};
  std::uint16_t fade_level = 0;
};

// Defines parameters of a constant force-feedback effect
// `level`: strength of the effect; may be negative
// `envelope`: envelope data
struct ConstantEffect : Effect {
  std::int16_t level = 0;
  Envelope envelope{};

  [[nodiscard]] static ForceFeedback StaticType() {
    return ForceFeedback::kConstant;
  }
  [[nodiscard]] ForceFeedback Type() const override { return StaticType(); }
  void ToData(void* data_ptr) const override;
  void FromData(const void* data_ptr) override;
  // Internal use.
  void CloneInplace(Effect* ptr) const noexcept override {
    new (ptr) ConstantEffect(*this);
  }
};
struct InertiaEffect : ConstantEffect {
  [[nodiscard]] ForceFeedback Type() const override {
    return ForceFeedback::kInertia;
  }
  // Internal use.
  void CloneInplace(Effect* ptr) const noexcept override {
    new (ptr) InertiaEffect(*this);
  }
};

// Defines parameters of a ramp force-feedback effect
// `start_level`: beginning strength of the effect; may be negative
// `end_level`: final strength of the effect; may be negative
// `envelope`: envelope data
struct RampEffect : Effect {
  std::int16_t start_level = 0;
  std::int16_t end_level = 0;
  Envelope envelope{};

  [[nodiscard]] static ForceFeedback StaticType() {
    return ForceFeedback::kRamp;
  }
  [[nodiscard]] ForceFeedback Type() const override { return StaticType(); }
  void ToData(void* data_ptr) const override;
  void FromData(const void* data_ptr) override;
  // Internal use.
  void CloneInplace(Effect* ptr) const noexcept override {
    new (ptr) RampEffect(*this);
  }
};

// Defines a spring, damper or friction force-feedback effect
// `right_saturation`: maximum level when joystick moved all way to the right
// `left_saturation`: same for the left side
// `right_coeff`: controls how fast the force grows when the joystick moves to
// the right `left_coeff`: same for the left side `deadband`: size of the dead
// zone, where no force is produced `center`: position of the dead zone
struct Condition {
  std::uint16_t right_saturation = 0;
  std::uint16_t left_saturation = 0;
  std::int16_t right_coeff = 0;
  std::int16_t left_coeff = 0;
  std::uint16_t deadband = 0;
  std::int16_t center = 0;
};
struct ConditionEffect : Effect {
  // One condition per axis.
  std::array<Condition, 2> conditions{};

  void ToData(void* data_ptr) const override;
  void FromData(const void* data_ptr) override;
  // Internal use.
  void CloneInplace(Effect* ptr) const noexcept override {
    new (ptr) ConditionEffect(*this);
  }
};

struct SpringEffect : ConditionEffect {
  [[nodiscard]] static ForceFeedback StaticType() {
    return ForceFeedback::kSpring;
  }
  [[nodiscard]] ForceFeedback Type() const override { return StaticType(); }
  // Internal use.
  void CloneInplace(Effect* ptr) const noexcept override {
    new (ptr) SpringEffect(*this);
  }
};
struct DamperEffect : ConditionEffect {
  [[nodiscard]] static ForceFeedback StaticType() {
    return ForceFeedback::kDamper;
  }
  [[nodiscard]] ForceFeedback Type() const override { return StaticType(); }
  // Internal use.
  void CloneInplace(Effect* ptr) const noexcept override {
    new (ptr) DamperEffect(*this);
  }
};
struct FrictionEffect : ConditionEffect {
  [[nodiscard]] static ForceFeedback StaticType() {
    return ForceFeedback::kFriction;
  }
  [[nodiscard]] ForceFeedback Type() const override { return StaticType(); }
  // Internal use.
  void CloneInplace(Effect* ptr) const noexcept override {
    new (ptr) FrictionEffect(*this);
  }
};

// Defines parameters of a periodic force-feedback effect
// `waveform`: kind of the effect (wave)
// `period`: period of the wave (ms)
// `magnitude`: peak value
// `offset`: mean value of the wave (roughly)
// `phase`: 'horizontal' shift
// `envelope`: envelope data
// `custom_len`: number of samples (FF_CUSTOM only)
// `custom_data`: buffer of samples (FF_CUSTOM only)
struct PeriodicEffect : Effect {
  // kSquare, kTriangle, kSine, kSawUp, kSawDown, kCustom
  ForceFeedback waveform{};
  absl::Duration period{};
  std::int16_t magnitude = 0;
  std::int16_t offset = 0;
  std::uint16_t phase = 0;
  Envelope envelope{};
  std::uint32_t custom_len = 0;
  std::int16_t* custom_data = nullptr;

  [[nodiscard]] static ForceFeedback StaticType() {
    return ForceFeedback::kPeriodic;
  }
  [[nodiscard]] ForceFeedback Type() const override { return StaticType(); }
  void ToData(void* data_ptr) const override;
  void FromData(const void* data_ptr) override;
  // Internal use.
  void CloneInplace(Effect* ptr) const noexcept override {
    new (ptr) PeriodicEffect(*this);
  }
};
struct CustomEffect : PeriodicEffect {
  [[nodiscard]] static ForceFeedback StaticType() {
    return ForceFeedback::kCustom;
  }
  [[nodiscard]] ForceFeedback Type() const override { return StaticType(); }
  // Internal use.
  void CloneInplace(Effect* ptr) const noexcept override {
    new (ptr) CustomEffect(*this);
  }
};

// Defines parameters of a periodic force-feedback effect
// `strong_magnitude`: magnitude of the heavy motor
// `weak_magnitude`: magnitude of the light one
//
// Some rumble pads have two motors of different weight. `strong_magnitude`
// represents the magnitude of the vibration generated by the heavy one.
struct RumbleEffect : Effect {
  std::uint16_t strong_magnitude = 0;
  std::uint16_t weak_magnitude = 0;

  [[nodiscard]] static ForceFeedback StaticType() {
    return ForceFeedback::kRumble;
  }
  [[nodiscard]] ForceFeedback Type() const override { return StaticType(); }
  void ToData(void* data_ptr) const override;
  void FromData(const void* data_ptr) override;
  // Internal use.
  void CloneInplace(Effect* ptr) const noexcept override {
    new (ptr) RumbleEffect(*this);
  }
};

class AnyEffect {
 public:
  explicit AnyEffect(const Effect& rhs) : data_{.base = rhs} {
    Base().~Effect();
    rhs.CloneInplace(&Base());
  }
  AnyEffect() : AnyEffect(Effect{}) {}
  AnyEffect(const AnyEffect& rhs) : AnyEffect(rhs.Base()) {}
  AnyEffect& operator=(const AnyEffect& rhs) noexcept {
    if (this == &rhs) {
      return *this;
    }
    Base().~Effect();
    rhs.Base().CloneInplace(&Base());
    return *this;
  }
  AnyEffect(AnyEffect&& rhs) noexcept : AnyEffect(rhs.Base()) {}
  AnyEffect& operator=(AnyEffect&& rhs) noexcept {
    return *this = static_cast<const AnyEffect&>(rhs);
  }
  ~AnyEffect() = default;

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  [[nodiscard]] Effect& Base() { return data_.base; }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  [[nodiscard]] const Effect& Base() const { return data_.base; }

  // Casting functions to get the derived-class effect.
  template <typename ActualEffect>
  [[nodiscard]] ActualEffect& As() {
    assert(ActualEffect::StaticType() == Base().Type());
    return static_cast<ActualEffect&>(Base());
  }
  template <typename ActualEffect>
  [[nodiscard]] const ActualEffect& As() const {
    assert(ActualEffect::StaticType() == Base().Type());
    return static_cast<const ActualEffect&>(Base());
  }

  void ToData(void* data_ptr) const { Base().ToData(data_ptr); }
  static AnyEffect FromData(const void* data_ptr);

 private:
  union EffectUnion {
    Effect base;
    ConstantEffect constant;
    RampEffect ramp;
    SpringEffect spring;
    DamperEffect damper;
    FrictionEffect friction;
    PeriodicEffect periodic;
    CustomEffect custom;
    RumbleEffect rumble;

    EffectUnion(const EffectUnion&) = delete;
    EffectUnion(EffectUnion&&) = delete;
    EffectUnion& operator=(const EffectUnion&) = delete;
    EffectUnion& operator=(EffectUnion&&) = delete;

    ~EffectUnion() { base.~Effect(); }
  } data_;
};

}  // namespace evdevpp

#endif  // EVDEVPP_EVDEVPP_INFO_H_
