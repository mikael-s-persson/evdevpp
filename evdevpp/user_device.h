#ifndef EVDEVPP_EVDEVPP_USER_DEVICE_H_
#define EVDEVPP_EVDEVPP_USER_DEVICE_H_

#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "evdevpp/device.h"
#include "evdevpp/ecodes.h"
#include "evdevpp/eventio.h"
#include "evdevpp/info.h"

namespace evdevpp {

// A userland input device and that can inject input events into the
// linux input subsystem.
class UserInputDevice : public EventIO {
 public:
  struct CreateOptions {
    // The event types and codes that the uinput device will be able to
    // inject - defaults to all key codes.
    CapabilitiesInfo capabilities = CapabilitiesInfo::AllKeys();
    // The name of the input device.
    std::string name = "evdevpp-uinput";
    // The device information fields of the input device.
    DeviceInfo info = {
        .bustype = BusType::kUsb,
        .vendor = 0x1,
        .product = 0x1,
        .version = 0x1,
    };
    // The device path.
    std::string devnode = "/dev/uinput";
    // Physical path.
    std::string phys = "evdevpp-uinput";
    // Input properties and quirks.
    std::vector<Property> input_props;
    // Maximum simultaneous force-feedback effects.
    int max_effects = ForceFeedback::kMaxEffects;
  };
  // Work-around for GCC/Clang bug.
  static CreateOptions Defaults() { return CreateOptions{}; }

  // Create a user input device from a given set of creation options.
  static absl::StatusOr<UserInputDevice> Create(
      const CreateOptions& options = Defaults());

  // Create a user input device with the capabilities of a list of devices.
  static absl::StatusOr<UserInputDevice> CreateFromDevices(
      const std::vector<InputDevice>& devices,
      const absl::flat_hash_set<std::uint16_t>& excluded_event_types =
          {EventType::kSyn, EventType::kFf},
      const CreateOptions& options = Defaults());

  // Create a user input device with the capabilities of a list of devices.
  static absl::StatusOr<UserInputDevice> CreateFromDevices(
      const std::vector<std::string>& device_filenames,
      const absl::flat_hash_set<std::uint16_t>& excluded_event_types =
          {EventType::kSyn, EventType::kFf},
      const CreateOptions& options = Defaults());

  absl::Status Close();

  UserInputDevice() = default;
  UserInputDevice(const UserInputDevice&) = default;
  UserInputDevice(UserInputDevice&&) = default;
  UserInputDevice& operator=(const UserInputDevice&) = default;
  UserInputDevice& operator=(UserInputDevice&&) = default;
  ~UserInputDevice() { (void)Close(); }

  [[nodiscard]] const DeviceInfo& Info() const { return info_; }
  [[nodiscard]] const std::string& Name() const { return name_; }
  [[nodiscard]] const std::string& Phys() const { return phys_; }
  [[nodiscard]] const std::string& DevPath() const { return devnode_; }
  [[nodiscard]] const InputDevice& Device() const { return device_; }
  [[nodiscard]] const CapabilitiesInfo& Capabilities() const {
    return capabilities_;
  }
  [[nodiscard]] const std::vector<Property>& Properties() const {
    return input_props_;
  }

  // Inject a `SYN_REPORT` event into the input subsystem.
  absl::Status Synchronize() const {
    return Write(EventType::kSyn, Synch::kReport, 0);
  }

  // Wait for an EventType::kUinput event that will signal us that an
  // effect upload/erase operation is in progress.
  //
  // if (event == UIForceFeedback::kUpload) {
  //   upload_or = device.BeginUpload(event.value);
  //   if (upload_or.ok()) {
  //     upload_or->retval = 0;
  //     fmt::print("[upload] effect_id: {}, type: {}", upload_or->effect.id,
  //     upload_or->effect.type); device.EndUpload(*upload_or);
  //   }
  // } else if (event == UIForceFeedback::kErase) {
  //   erase_or = device.BeginErase(event.value);
  //   if (erase_or.ok()) {
  //     fmt::print("[erase] effect_id: {}", erase_or->effect_id);
  //     erase_or->retval = 0;
  //     device.EndErase(*erase_or);
  //   }
  // }
  absl::StatusOr<UInputUpload> BeginUpload(std::uint32_t request_id) const;
  absl::Status EndUpload(const UInputUpload& upload) const;

  absl::StatusOr<UInputErase> BeginErase(std::uint32_t effect_id) const;
  absl::Status EndErase(const UInputErase& erase) const;

 private:
  DeviceInfo info_;
  std::string name_;
  std::string phys_;
  std::string devnode_;
  CapabilitiesInfo capabilities_;
  std::vector<Property> input_props_;
  InputDevice device_;

  absl::Status Setup(std::uint32_t max_effects);
};

}  // namespace evdevpp

#endif  // EVDEVPP_EVDEVPP_USER_DEVICE_H_
