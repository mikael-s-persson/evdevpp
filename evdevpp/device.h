#ifndef EVDEVPP_EVDEVPP_DEVICE_H_
#define EVDEVPP_EVDEVPP_DEVICE_H_

#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "evdevpp/ecodes.h"
#include "evdevpp/eventio.h"
#include "evdevpp/info.h"

namespace evdevpp {

// List readable character devices in `input_device_dir`.
std::vector<std::string> ListDevices(
    std::string_view input_device_dir = "/dev/input");

// Check if `filename` is a readable and writable character device.
bool IsDevice(const std::string& filename);

// A linux input device from which input events can be read.
class InputDevice : public EventIO {
 public:
  static absl::StatusOr<InputDevice> Open(const std::string& dev_path);

  // Grab input device using `EVIOCGRAB` - other applications will
  // be unable to receive events until the device is released. Only
  // one process can hold a `EVIOCGRAB` on a device.
  //
  // Warning:
  // Grabbing an already grabbed device will fail.
  absl::Status Grab() const;

  // Release device if it has been already grabbed (uses `EVIOCGRAB`).
  //
  // Warning:
  // Releasing an already released device will fail.
  absl::Status Ungrab() const;

  struct ScopedGrab {
    const InputDevice* parent;
    explicit ScopedGrab(const InputDevice* parent_) : parent(parent_) {}
    ScopedGrab(const ScopedGrab&) = delete;
    ScopedGrab& operator=(const ScopedGrab&) = delete;
    ScopedGrab(ScopedGrab&& rhs) noexcept : parent(rhs.parent) {
      rhs.parent = nullptr;
    }
    ScopedGrab& operator=(ScopedGrab&& rhs) noexcept {
      ScopedGrab tmp{std::move(*this)};
      parent = rhs.parent;
      rhs.parent = nullptr;
      return *this;
    }
    ~ScopedGrab() {
      if (parent != nullptr) {
        (void)parent->Ungrab();
      }
    }
  };
  // Grab the device (see `Grab`) and automatically ungrab it upon destruction.
  absl::StatusOr<ScopedGrab> GrabInScope() const {
    absl::Status st = Grab();
    if (!st.ok()) {
      return st;
    }
    return ScopedGrab(this);
  }

  // Get device properties and quirks.
  [[nodiscard]] absl::StatusOr<absl::flat_hash_set<std::uint16_t>> Properties()
      const;

  [[nodiscard]] const DeviceInfo& Info() const { return info_; }
  [[nodiscard]] const std::string& DevPath() const { return path_; }
  [[nodiscard]] const std::string& Name() const { return name_; }
  [[nodiscard]] int Version() const { return version_; }
  [[nodiscard]] const std::string& Phys() const { return phys_; }
  [[nodiscard]] const std::string& Uniq() const { return uniq_; }
  [[nodiscard]] const CapabilitiesInfo& Capabilities() const {
    return capabilities_;
  }
  [[nodiscard]] int FFEffectsCount() const { return ff_effects_count_; }

  absl::Status SetAbsoluteAxisInfo(AbsoluteAxis axis, const AbsInfo& abs_info);

  [[nodiscard]] absl::StatusOr<absl::flat_hash_set<std::uint16_t>>
  GetActiveKeys() const;

  [[nodiscard]] absl::StatusOr<KeyRepeatInfo> GetRepeat() const;
  absl::Status SetRepeat(const KeyRepeatInfo& rep_info) const;

  // Return currently set LED keys.
  [[nodiscard]] absl::StatusOr<absl::flat_hash_set<std::uint16_t>> LEDs() const;
  // Set the state of the selected LED.
  absl::Status SetLED(LED ev, std::int32_t value) const {
    return Write(EventType::kLed, ev, value);
  }

  // Upload a force feedback effect to a force feedback device.
  [[nodiscard]] absl::StatusOr<std::int16_t> UploadEffect(
      const AnyEffect& new_effect) const;

  // Erase a force effect from a force feedback device. This also
  // stops the effect.
  absl::Status EraseEffect(int id) const;

 private:
  std::string path_;
  DeviceInfo info_{};
  std::string name_;
  std::string phys_;
  std::string uniq_;
  int version_ = 0;
  CapabilitiesInfo capabilities_;
  int ff_effects_count_ = 0;
};

}  // namespace evdevpp

#endif  // EVDEVPP_EVDEVPP_DEVICE_H_
