#include "evdevpp/eventio.h"

#include <poll.h>

#include "evdevpp/events.h"
#include "linux/input.h"

namespace evdevpp {

absl::StatusOr<bool> EventIO::Wait(absl::Duration timeout) const {
  struct pollfd pfd = {.fd = fd_.Fd(), .events = POLLIN};
  int poll_res =
      ::poll(&pfd, 1,
             std::min(1, static_cast<int>(absl::ToInt64Milliseconds(timeout))));
  if (poll_res < 0) {
    return absl::ErrnoToStatus(errno, "Wait on input event failed");
  }
  return (poll_res != 0);
}

absl::StatusOr<InputEvent> EventIO::ReadOne() const {
  struct input_event event;

  int n = ::read(fd_.Fd(), &event, sizeof(event));

  if (n < 0) {
    return absl::ErrnoToStatus(errno, "ReadOne input event failed");
  }

  InputEvent result{absl::TimeFromTimeval({.tv_sec = event.input_event_sec,
                                           .tv_usec = event.input_event_usec}),
                    event.type, event.code, event.value};
  return result;
}

absl::StatusOr<std::vector<InputEvent>> EventIO::ReadAll() const {
  std::vector<InputEvent> result;

  std::array<struct input_event, 64> events;
  constexpr std::size_t event_size = sizeof(struct input_event);

  while (true) {
    int nread = ::read(fd_.Fd(), events.data(), event_size * events.size());

    if (nread < 0) {
      if (errno == EAGAIN) {
        return result;
      }
      return absl::ErrnoToStatus(errno, "ReadAll input event failed");
    }

    // Add the list of events
    for (unsigned i = 0; i < nread / event_size; i++) {
      result.emplace_back(
          absl::TimeFromTimeval({.tv_sec = events[i].input_event_sec,
                                 .tv_usec = events[i].input_event_usec}),
          events[i].type, events[i].code, events[i].value);
    }
  }
}

absl::Status EventIO::Write(std::uint16_t etype, std::uint16_t code,
                            std::int32_t value) const {
  struct input_event event;
  struct timeval tval = absl::ToTimeval(absl::Now());
  event.input_event_usec = tval.tv_usec;
  event.input_event_sec = tval.tv_sec;
  event.type = etype;
  event.code = code;
  event.value = value;

  if (::write(fd_.Fd(), &event, sizeof(event)) != sizeof(event)) {
    return absl::ErrnoToStatus(errno, "error writing event to uinput device");
  }
  return absl::OkStatus();
}

}  // namespace evdevpp
