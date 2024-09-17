#include "evdevpp/events.h"

#include "evdevpp/ecodes.h"

namespace evdevpp {

namespace {

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
  (((!CheckType || ExpectedEventType<CatEvents>()) == uncategorized_ev.type &&
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
