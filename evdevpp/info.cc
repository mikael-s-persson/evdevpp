#include "evdevpp/info.h"

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

void Effect::ToData(void* data_ptr) const {
  ff_effect* effect = static_cast<ff_effect*>(data_ptr);
  effect->type = type();
  effect->id = id;
  effect->direction = direction;
  effect->trigger = {.button = trigger.button,
                     .interval = ToInt16Milliseconds(trigger.interval)};
  effect->replay = {.length = ToInt16Milliseconds(replay.length),
                    .delay = ToInt16Milliseconds(replay.delay)};
}

void Effect::FromData(const void* data_ptr) {
  const ff_effect* effect = static_cast<const ff_effect*>(data_ptr);
  id = effect->id;
  direction = effect->direction;
  trigger = {.button = effect->trigger.button,
             .interval = absl::Milliseconds(effect->trigger.interval)};
  replay = {.length = absl::Milliseconds(effect->replay.length),
            .delay = absl::Milliseconds(effect->replay.delay)};
}

void ConstantEffect::ToData(void* data_ptr) const {
  Effect::ToData(data_ptr);
  ff_constant_effect& constant = static_cast<ff_effect*>(data_ptr)->u.constant;
  constant.level = level;
  constant.envelope = ToEnvelope(envelope);
}

void ConstantEffect::FromData(const void* data_ptr) {
  Effect::FromData(data_ptr);
  const ff_constant_effect& constant =
      static_cast<const ff_effect*>(data_ptr)->u.constant;
  level = constant.level;
  envelope = FromEnvelope(constant.envelope);
}

void RampEffect::ToData(void* data_ptr) const {
  Effect::ToData(data_ptr);
  ff_ramp_effect& ramp = static_cast<ff_effect*>(data_ptr)->u.ramp;
  ramp.start_level = start_level;
  ramp.end_level = end_level;
  ramp.envelope = ToEnvelope(envelope);
}

void RampEffect::FromData(const void* data_ptr) {
  Effect::FromData(data_ptr);
  const ff_ramp_effect& ramp = static_cast<const ff_effect*>(data_ptr)->u.ramp;
  start_level = ramp.start_level;
  end_level = ramp.end_level;
  envelope = FromEnvelope(ramp.envelope);
}

void ConditionEffect::ToData(void* data_ptr) const {
  Effect::ToData(data_ptr);
  ff_effect* effect = static_cast<ff_effect*>(data_ptr);
  for (int i = 0; i < 2; ++i) {
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
  const ff_effect* effect = static_cast<const ff_effect*>(data_ptr);
  for (int i = 0; i < 2; ++i) {
    conditions[i] = {
        .right_saturation = effect->u.condition[i].right_saturation,
        .left_saturation = effect->u.condition[i].left_saturation,
        .right_coeff = effect->u.condition[i].right_coeff,
        .left_coeff = effect->u.condition[i].left_coeff,
        .deadband = effect->u.condition[i].deadband,
        .center = effect->u.condition[i].center,
    };
  }
}

void PeriodicEffect::ToData(void* data_ptr) const {
  Effect::ToData(data_ptr);
  ff_periodic_effect& periodic = static_cast<ff_effect*>(data_ptr)->u.periodic;
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
  const ff_periodic_effect& periodic =
      static_cast<const ff_effect*>(data_ptr)->u.periodic;
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
  ff_rumble_effect& rumble = static_cast<ff_effect*>(data_ptr)->u.rumble;
  rumble.strong_magnitude = strong_magnitude;
  rumble.weak_magnitude = weak_magnitude;
}

void RumbleEffect::FromData(const void* data_ptr) {
  Effect::FromData(data_ptr);
  const ff_rumble_effect& rumble =
      static_cast<const ff_effect*>(data_ptr)->u.rumble;
  strong_magnitude = rumble.strong_magnitude;
  weak_magnitude = rumble.weak_magnitude;
}

AnyEffect AnyEffect::FromData(const void* data_ptr) {
  const ff_effect* effect = static_cast<const ff_effect*>(data_ptr);
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
  result.base().FromData(data_ptr);
  return result;
}

}  // namespace evdevpp
