#include "evdevpp/events.h"

#include "evdevpp/ecodes.h"

namespace evdevpp {

namespace {

template <typename CatEvent>
bool CheckEventType(EventType ev_type) {
  if constexpr (std::is_same_v<CatEvent, KeyEvent>) {
    return ev_type == EventType::kKey;
  } else if constexpr (std::is_same_v<CatEvent, SynchEvent>) {
    return ev_type == EventType::kSyn;
  } else if constexpr (std::is_same_v<CatEvent, RelEvent>) {
    return ev_type == EventType::kRel;
  } else if constexpr (std::is_same_v<CatEvent, AbsEvent>) {
    return ev_type == EventType::kAbs;
  } else if constexpr (std::is_same_v<CatEvent, MiscEvent>) {
    return ev_type == EventType::kMsc;
  } else if constexpr (std::is_same_v<CatEvent, SwitchEvent>) {
    return ev_type == EventType::kSw;
  } else if constexpr (std::is_same_v<CatEvent, LEDEvent>) {
    return ev_type == EventType::kLed;
  } else if constexpr (std::is_same_v<CatEvent, SoundEvent>) {
    return ev_type == EventType::kSnd;
  } else if constexpr (std::is_same_v<CatEvent, AutorepeatEvent>) {
    return ev_type == EventType::kRep;
  } else if constexpr (std::is_same_v<CatEvent, ForceFeedbackEvent>) {
    return ev_type == EventType::kFfStatus;
  } else if constexpr (std::is_same_v<CatEvent, UIForceFeedbackEvent>) {
    return ev_type == EventType::kUinput;
  } else {
    return false;
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
  (((!CheckType || CheckEventType<CatEvents>(uncategorized_ev.type)) &&
    set_if_match(CatEvents{uncategorized_ev})) ||
   ... || false);
  return result;
}

}  // namespace

AnyInputEvent AnyInputEvent::Categorize(const InputEvent& uncategorized_ev) {
  // Start by checking the declared type.
  AnyInputEvent result =
      CategorizeImpl<true, KeyEvent, RelEvent, AbsEvent, SynchEvent, MiscEvent,
                     SwitchEvent, LEDEvent, SoundEvent, AutorepeatEvent,
                     ForceFeedbackEvent, UIForceFeedbackEvent>(
          uncategorized_ev);
  if (result.Base().IsInCategory()) {
    return result;
  }
  // Fall back to trying all event code types.
  return CategorizeImpl<false, KeyEvent, RelEvent, AbsEvent, SynchEvent,
                        MiscEvent, SwitchEvent, LEDEvent, SoundEvent,
                        AutorepeatEvent, ForceFeedbackEvent,
                        UIForceFeedbackEvent>(uncategorized_ev);
}

}  // namespace evdevpp
