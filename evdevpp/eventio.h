#ifndef EVDEVPP_EVDEVPP_EVENTIO_H_
#define EVDEVPP_EVDEVPP_EVENTIO_H_

#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "evdevpp/events.h"
#include "evdevpp/info.h"
#include "toolbelt/fd.h"

namespace evdevpp {

// Base class for reading and writing input events.
//
// This class is used by `InputDevice` and `UInput`.
//
//  - On, `InputDevice` it used for reading user-generated events (e.g.
//    key presses, mouse movements) and writing feedback events (e.g. leds,
//    beeps).
//
//  - On, `UInput` it used for writing user-generated events (e.g.
//    key presses, mouse movements) and reading feedback events (e.g. leds,
//    beeps).
class EventIO {
 public:
  void Close() { fd_.Close(); }

  [[nodiscard]] bool IsOpen() const { return fd_.IsOpen(); }

  // Return the file descriptor to the open event device.
  [[nodiscard]] toolbelt::FileDescriptor Fd() const { return fd_; }

  // Wait for the device to have an event ready to read.
  // Returns true if an event is available.
  // Returns false if the wait timed out.
  // Returns a status for a system error (errno).
  [[nodiscard]] absl::StatusOr<bool> Wait(absl::Duration timeout) const;

  // Read and return a single input event as an `InputEvent`.
  // A status `IsUnavailable(status)` means no pending input events.
  // Other returned statuses are system errors (errno).
  [[nodiscard]] absl::StatusOr<InputEvent> ReadOne() const;

  // Read multiple input events from device. Return a vector of `InputEvent`.
  // Returned status indicates a system error.
  [[nodiscard]] absl::StatusOr<std::vector<InputEvent>> ReadAll() const;

  // Inject an input event into the input subsystem. Events are
  // queued until a synchronization event is received.
  //
  // Example
  //  ev = InputEvent(1334414993, 274296, ecodes.EV_KEY, ecodes.KEY_A, 1)
  //  ui.Write(ev)
  absl::Status Write(std::uint16_t etype, std::uint16_t code,
                     std::int32_t value) const;

  absl::Status Write(const InputEvent& event) const {
    return Write(event.type, event.code, event.value);
  }

 protected:
  toolbelt::FileDescriptor fd_;
};

}  // namespace evdevpp

#endif  // EVDEVPP_EVDEVPP_EVENTIO_H_
