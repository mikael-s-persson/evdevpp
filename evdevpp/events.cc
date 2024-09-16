#include "evdevpp/events.h"

#include "evdevpp/ecodes.h"

namespace evdevpp {

namespace {

template <typename ECodeType>
EventType ExpectedEventType() {
  if constexpr (std::is_same_v<ECodeType, Key>) {
    return EventType::kKey;
  } else if constexpr (std::is_same_v<ECodeType, Button>) {
    return EventType::kKey;
  } else if constexpr (std::is_same_v<ECodeType, Synch>) {
    return EventType::kSyn;
  } else if constexpr (std::is_same_v<ECodeType, RelativeAxis>) {
    return EventType::kRel;
  } else if constexpr (std::is_same_v<ECodeType, AbsoluteAxis>) {
    return EventType::kAbs;
  } else if constexpr (std::is_same_v<ECodeType, Misc>) {
    return EventType::kMsc;
  } else if constexpr (std::is_same_v<ECodeType, Switch>) {
    return EventType::kSw;
  } else if constexpr (std::is_same_v<ECodeType, LED>) {
    return EventType::kLed;
  } else if constexpr (std::is_same_v<ECodeType, Sound>) {
    return EventType::kSnd;
  } else if constexpr (std::is_same_v<ECodeType, Autorepeat>) {
    return EventType::kRep;
  } else {
    return EventType::kMax;
  }
};

template <bool CheckType, typename... CatEvents>
AnyInputEvent CategorizeImpl(InputEvent uncategorized_ev) {
  AnyInputEvent result{uncategorized_ev};
  auto set_if_match = [&](auto cat_ev) {
    if (cat_ev.IsInCategory()) {
      result = AnyInputEvent{cat_ev};
      return true;
    }
    return false;
  };
  (((!CheckType || uncategorized_ev.type == ExpectedEventType<CatEvents>()) &&
    set_if_match(CatEvents{uncategorized_ev})) ||
   ... || false);
  return result;
}

}  // namespace

AnyInputEvent AnyInputEvent::Categorize(InputEvent uncategorized_ev) {
  // Start by checking the declared type.
  AnyInputEvent result =
      CategorizeImpl<true, KeyEvent, RelEvent, AbsEvent, SynchEvent, MiscEvent,
                     SwitchEvent, LEDEvent, SoundEvent, AutorepeatEvent>(
          uncategorized_ev);
  if (result.base().IsInCategory()) {
    return result;
  }
  // Fall back to trying all event code types.
  return CategorizeImpl<false, KeyEvent, RelEvent, AbsEvent, SynchEvent,
                        MiscEvent, SwitchEvent, LEDEvent, SoundEvent,
                        AutorepeatEvent>(uncategorized_ev);
}

}  // namespace evdevpp
