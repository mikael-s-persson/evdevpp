#include "evdevpp/info.h"

#include <type_traits>

#include "linux/input.h"

namespace evdevpp {

namespace {

std::uint16_t ToInt16Milliseconds(absl::Duration d) {
  return static_cast<std::uint16_t>(absl::ToInt64Milliseconds(d));
}

ff_envelope ToEnvelope(const Envelope& envelope) {
  return {.attack_length = ToInt16Milliseconds(envelope.attack_length),
          .attack_level = envelope.attack_level,
          .fade_length = ToInt16Milliseconds(envelope.fade_length),
          .fade_level = envelope.fade_level};
}

Envelope FromEnvelope(const ff_envelope& envelope) {
  return {.attack_length = absl::Milliseconds(envelope.attack_length),
          .attack_level = envelope.attack_level,
          .fade_length = absl::Milliseconds(envelope.fade_length),
          .fade_level = envelope.fade_level};
}

template <typename Cont>
bool ContainsAll(const Cont& superset, const Cont& subset) {
  return std::all_of(
      subset.begin(), subset.end(), [&superset](const auto& elem) {
        if constexpr (std::is_convertible_v<decltype(elem), std::uint16_t>) {
          return superset.contains(elem);
        } else {
          return superset.contains(elem.first);
        }
      });
}

}  // namespace

CapabilitiesInfo CapabilitiesInfo::AllKeys() {
  CapabilitiesInfo result;
  const auto& keys = Key::CodeToString();
  const auto& buttons = Button::CodeToString();
  result.keys.reserve(keys.size() + buttons.size());
  for (auto [code, str] : keys) {
    result.keys.insert(code);
  }
  for (auto [code, str] : buttons) {
    result.keys.insert(code);
  }
  return result;
}

bool CapabilitiesInfo::HasCapabilities(const CapabilitiesInfo& min_caps) const {
  return ContainsAll(keys, min_caps.keys) &&
         ContainsAll(synchs, min_caps.synchs) &&
         ContainsAll(relative_axes, min_caps.relative_axes) &&
         ContainsAll(absolute_axes, min_caps.absolute_axes) &&
         ContainsAll(miscs, min_caps.miscs) &&
         ContainsAll(switches, min_caps.switches) &&
         ContainsAll(leds, min_caps.leds) &&
         ContainsAll(sounds, min_caps.sounds) &&
         ContainsAll(autorepeats, min_caps.autorepeats) &&
         ContainsAll(force_feedbacks, min_caps.force_feedbacks) &&
         ContainsAll(uinputs, min_caps.uinputs);
}

void Effect::ToData(void* data_ptr) const {
  auto* effect = static_cast<ff_effect*>(data_ptr);
  effect->type = Type();
  effect->id = id;
  effect->direction = direction;
  effect->trigger = {.button = trigger.button,
                     .interval = ToInt16Milliseconds(trigger.interval)};
  effect->replay = {.length = ToInt16Milliseconds(replay.length),
                    .delay = ToInt16Milliseconds(replay.delay)};
}

void Effect::FromData(const void* data_ptr) {
  const auto* effect = static_cast<const ff_effect*>(data_ptr);
  id = effect->id;
  direction = effect->direction;
  trigger = {.button = effect->trigger.button,
             .interval = absl::Milliseconds(effect->trigger.interval)};
  replay = {.length = absl::Milliseconds(effect->replay.length),
            .delay = absl::Milliseconds(effect->replay.delay)};
}

void ConstantEffect::ToData(void* data_ptr) const {
  Effect::ToData(data_ptr);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  auto& constant = static_cast<ff_effect*>(data_ptr)->u.constant;
  constant.level = level;
  constant.envelope = ToEnvelope(envelope);
}

void ConstantEffect::FromData(const void* data_ptr) {
  Effect::FromData(data_ptr);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  const auto& constant = static_cast<const ff_effect*>(data_ptr)->u.constant;
  level = constant.level;
  envelope = FromEnvelope(constant.envelope);
}

void RampEffect::ToData(void* data_ptr) const {
  Effect::ToData(data_ptr);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  auto& ramp = static_cast<ff_effect*>(data_ptr)->u.ramp;
  ramp.start_level = start_level;
  ramp.end_level = end_level;
  ramp.envelope = ToEnvelope(envelope);
}

void RampEffect::FromData(const void* data_ptr) {
  Effect::FromData(data_ptr);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  const auto& ramp = static_cast<const ff_effect*>(data_ptr)->u.ramp;
  start_level = ramp.start_level;
  end_level = ramp.end_level;
  envelope = FromEnvelope(ramp.envelope);
}

void ConditionEffect::ToData(void* data_ptr) const {
  Effect::ToData(data_ptr);
  auto* effect = static_cast<ff_effect*>(data_ptr);
  for (int i = 0; i < 2; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    effect->u.condition[i] = {
        .right_saturation = conditions[i].right_saturation,
        .left_saturation = conditions[i].left_saturation,
        .right_coeff = conditions[i].right_coeff,
        .left_coeff = conditions[i].left_coeff,
        .deadband = conditions[i].deadband,
        .center = conditions[i].center,
    };
  }
}

void ConditionEffect::FromData(const void* data_ptr) {
  Effect::FromData(data_ptr);
  const auto* effect = static_cast<const ff_effect*>(data_ptr);
  for (int i = 0; i < 2; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    const auto& cond_in = effect->u.condition[i];
    conditions[i] = {
        .right_saturation = cond_in.right_saturation,
        .left_saturation = cond_in.left_saturation,
        .right_coeff = cond_in.right_coeff,
        .left_coeff = cond_in.left_coeff,
        .deadband = cond_in.deadband,
        .center = cond_in.center,
    };
  }
}

void PeriodicEffect::ToData(void* data_ptr) const {
  Effect::ToData(data_ptr);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  auto& periodic = static_cast<ff_effect*>(data_ptr)->u.periodic;
  periodic.waveform = waveform;
  periodic.period = ToInt16Milliseconds(period);
  periodic.magnitude = magnitude;
  periodic.offset = offset;
  periodic.phase = phase;
  periodic.envelope = ToEnvelope(envelope);
  periodic.custom_len = custom_len;
  periodic.custom_data = custom_data;
}

void PeriodicEffect::FromData(const void* data_ptr) {
  Effect::FromData(data_ptr);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  const auto& periodic = static_cast<const ff_effect*>(data_ptr)->u.periodic;
  waveform = periodic.waveform;
  period = absl::Milliseconds(periodic.period);
  magnitude = periodic.magnitude;
  offset = periodic.offset;
  phase = periodic.phase;
  envelope = FromEnvelope(periodic.envelope);
  custom_len = periodic.custom_len;
  custom_data = periodic.custom_data;
}

void RumbleEffect::ToData(void* data_ptr) const {
  Effect::ToData(data_ptr);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  auto& rumble = static_cast<ff_effect*>(data_ptr)->u.rumble;
  rumble.strong_magnitude = strong_magnitude;
  rumble.weak_magnitude = weak_magnitude;
}

void RumbleEffect::FromData(const void* data_ptr) {
  Effect::FromData(data_ptr);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
  const auto& rumble = static_cast<const ff_effect*>(data_ptr)->u.rumble;
  strong_magnitude = rumble.strong_magnitude;
  weak_magnitude = rumble.weak_magnitude;
}

AnyEffect AnyEffect::FromData(const void* data_ptr) {
  const auto* effect = static_cast<const ff_effect*>(data_ptr);
  AnyEffect result;
  if (effect->type == ForceFeedback::kConstant) {
    result = AnyEffect(ConstantEffect{});
  } else if (effect->type == ForceFeedback::kPeriodic) {
    result = AnyEffect(PeriodicEffect{});
  } else if (effect->type == ForceFeedback::kRamp) {
    result = AnyEffect(RampEffect{});
  } else if (effect->type == ForceFeedback::kSpring) {
    result = AnyEffect(SpringEffect{});
  } else if (effect->type == ForceFeedback::kFriction) {
    result = AnyEffect(FrictionEffect{});
  } else if (effect->type == ForceFeedback::kDamper) {
    result = AnyEffect(DamperEffect{});
  } else if (effect->type == ForceFeedback::kRumble) {
    result = AnyEffect(RumbleEffect{});
  } else if (effect->type == ForceFeedback::kInertia) {
    result = AnyEffect(InertiaEffect{});
  } else if (effect->type == ForceFeedback::kCustom) {
    result = AnyEffect(CustomEffect{});
  }
  result.Base().FromData(data_ptr);
  return result;
}

}  // namespace evdevpp
