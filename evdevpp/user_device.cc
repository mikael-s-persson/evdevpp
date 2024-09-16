#include "evdevpp/user_device.h"

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <type_traits>

#include "evdevpp/util.h"
#include "evdevpp/info.h"
#include "linux/uinput.h"

namespace evdevpp {

namespace {

template <typename CodeSet>
absl::Status EnableEventCodes(int fd, std::uint16_t etype,
                              const CodeSet& codes) {
  unsigned long req = 0;
  switch (etype) {
    case EV_KEY:
      req = UI_SET_KEYBIT;
      break;
    case EV_ABS:
      req = UI_SET_ABSBIT;
      break;
    case EV_REL:
      req = UI_SET_RELBIT;
      break;
    case EV_MSC:
      req = UI_SET_MSCBIT;
      break;
    case EV_SW:
      req = UI_SET_SWBIT;
      break;
    case EV_LED:
      req = UI_SET_LEDBIT;
      break;
    case EV_FF:
      req = UI_SET_FFBIT;
      break;
    case EV_SND:
      req = UI_SET_SNDBIT;
      break;
    default:
      return absl::InvalidArgumentError(
          fmt::format("Unsupported event type {} (0x{:X})",
                      EventType{etype}.ToString(), etype));
  }

  if (ioctl(fd, UI_SET_EVBIT, etype) < 0) {
    return absl::ErrnoToStatus(errno, "Failed to enable event type");
  }

  for (const auto& code : codes) {
    std::uint16_t ev_code = 0;
    if constexpr (std::is_convertible_v<decltype(code), std::uint16_t>) {
      ev_code = code;
    } else {
      ev_code = code.first;
    }
    if (ioctl(fd, req, ev_code) < 0) {
      return absl::ErrnoToStatus(errno, "Failed to enable event code");
    }
  }
  return absl::OkStatus();
}

template <typename Cont>
void MergeIfNotExcluded(std::uint16_t etype,
                        const absl::flat_hash_set<std::uint16_t>& excl,
                        Cont& into, const Cont& from) {
  if (!excl.contains(etype)) {
    into.insert(from.begin(), from.end());
  }
}

// Tries to find the device node when running on Linux.
absl::StatusOr<InputDevice> FindDeviceLinux(const std::string& sysname) {
  // The sysfs entry for event devices should contain exactly one folder
  // whose name matches the format "event[0-9]+". It is then assumed that
  // the device node in /dev/input uses the same name.
  std::string syspath = fmt::format("/sys/devices/virtual/input/{}", sysname);
  std::string device_path;
  for (const auto& dev_filename :
       std::filesystem::directory_iterator(syspath)) {
    std::string_view dev_basename = dev_filename.path().filename().native();
    if (dev_basename.substr(0, 5) == "event" &&
        std::all_of(dev_basename.begin() + 5, dev_basename.end(),
                    [](char c) { return std::isdigit(c) != 0; })) {
      device_path = fmt::format("/dev/input/{}", dev_basename);
      break;
    }
  }
  if (device_path.empty()) {
    return absl::NotFoundError(
        fmt::format("Could not find device in {}", syspath));
  }

  // It is possible that there is some delay before /dev/input/event* shows
  // up on old systems that do not use devtmpfs, so if the device cannot be
  // found, wait for a short amount and then try again once.
  //
  // Furthermore, even if devtmpfs is in use, it is possible that the device
  // does show up immediately, but without the correct permissions that
  // still need to be set by udev. Wait for up to two seconds for either the
  // device to show up or the permissions to be set.
  for (int attempt = 0; attempt < 19; ++attempt) {
    if (auto dev_or = InputDevice::Open(device_path); dev_or.ok()) {
      return std::move(*dev_or);
    } else {
      absl::SleepFor(absl::Milliseconds(100));
      continue;
    }
  }

  // Last attempt. If this fails, the status the last attempt is returned.
  return InputDevice::Open(device_path);
}

// Tries to find the device node when UI_GET_SYSNAME is not available or
// we're running on a system sufficiently exotic that we do not know how
// to interpret its return value.
absl::StatusOr<InputDevice> FindDeviceFallback(const std::string& ui_name) {
  // bug: the device node might not be immediately available
  absl::SleepFor(absl::Milliseconds(100));

  // There could also be another device with the same name already present,
  // make sure to select the newest one.
  // Strictly speaking, we cannot be certain that everything returned by
  // list_devices() ends at event[0-9]+: it might return something like
  // "/dev/input/events_all". Find the devices that have the expected structure
  // only.
  std::vector<std::string> all_devices = list_devices("/dev/input");
  std::vector<std::string> candidate_devices;
  candidate_devices.reserve(all_devices.size());
  for (const auto& dev_path : all_devices) {
    std::string_view dev_basename =
        std::filesystem::path{dev_path}.filename().native();
    if (dev_basename.substr(0, 5) == "event" &&
        std::all_of(dev_basename.begin() + 5, dev_basename.end(),
                    [](char c) { return std::isdigit(c) != 0; })) {
      candidate_devices.emplace_back(
          fmt::format("/dev/input/{}", dev_basename));
    }
  }
  // The modification date of the devnode is not reliable unfortunately, so we
  // are sorting by the number in the name.
  std::sort(candidate_devices.begin(), candidate_devices.end());

  for (const auto& dev_path : candidate_devices) {
    if (auto dev_or = InputDevice::Open(dev_path);
        dev_or.ok() && dev_or->Name() == ui_name) {
      return std::move(*dev_or);
    }
  }
  return absl::NotFoundError(
      fmt::format("Could not find device matching name '{}'", ui_name));
}

// Tries to find the device node. Will delegate this task to one of
// several platform-specific functions.
absl::StatusOr<InputDevice> FindDevice(int fd, const std::string& ui_name) {
// If we have a recent Linux kernel, this should work.
#if defined(__linux__) && defined(UI_GET_SYSNAME)
  char sysname[64];
  if (ioctl(fd, UI_GET_SYSNAME(sizeof(sysname)), &sysname) >= 0) {
    auto dev_or = FindDeviceLinux(sysname);
    if (dev_or.ok()) {
      return std::move(*dev_or);
    }
  }
#endif

  // If we're not running on Linux or the above method fails for any reason,
  // use the generic fallback method.
  return FindDeviceFallback(ui_name);
}

}  // namespace

absl::Status UserInputDevice::Setup(std::uint32_t max_effects) {
// Different kernel versions have different device setup methods. You can read
// more about it here:
// https://github.com/torvalds/linux/commit/052876f8e5aec887d22c4d06e54aa5531ffcec75

// Setup function for kernel >= v4.5
#if defined(UI_DEV_SETUP) && defined(UI_ABS_SETUP)

  // Setup absinfo:
  for (const auto& [code, absinfo] : capabilities_.absolute_axes) {
    uinput_abs_setup abs_setup{};
    abs_setup.code = code;
    abs_setup.absinfo.value = absinfo.value;
    abs_setup.absinfo.minimum = absinfo.minimum;
    abs_setup.absinfo.maximum = absinfo.maximum;
    abs_setup.absinfo.fuzz = absinfo.fuzz;
    abs_setup.absinfo.flat = absinfo.flat;
    abs_setup.absinfo.resolution = absinfo.resolution;

    if (ioctl(fd_.Fd(), UI_ABS_SETUP, &abs_setup) < 0) {
      return absl::ErrnoToStatus(errno, "Failed to setup absolute axis");
    }
  }

  // Setup evdev:
  uinput_setup usetup{};
  strncpy(usetup.name, name_.c_str(), sizeof(usetup.name) - 1);
  usetup.id.vendor = info_.vendor;
  usetup.id.product = info_.product;
  usetup.id.version = info_.version;
  usetup.id.bustype = info_.bustype;
  usetup.ff_effects_max = max_effects;

  if (ioctl(fd_.Fd(), UI_DEV_SETUP, &usetup) < 0) {
    return absl::ErrnoToStatus(errno, "Failed to setup user device info");
  }
// Fallback setup function (Linux <= 4.5 and FreeBSD).
#else
  uinput_user_dev uidev{};
  strncpy(uidev.name, name_.c_str(), sizeof(uidev.name) - 1);
  uidev.id.vendor = info_.vendor;
  uidev.id.product = info_.product;
  uidev.id.version = info_.version;
  uidev.id.bustype = info_.bustype;
  uidev.ff_effects_max = max_effects;

  for (const auto& [code, absinfo] : capabilities_.absolute_axes) {
    uidev.absmin[code] = absinfo.minimum;
    uidev.absmax[code] = absinfo.maximum;
    uidev.absfuzz[code] = absinfo.fuzz;
    uidev.absflat[code] = absinfo.flat;
  }

  if (write(fd_.Fd(), &uidev, sizeof(uidev)) != sizeof(uidev)) {
    return absl::ErrnoToStatus(errno, "Failed to setup user device info");
  }
#endif
  return absl::OkStatus();
}

absl::StatusOr<UserInputDevice> UserInputDevice::Create(
    const CreateOptions& options) {
  // Verify that an uinput device exists and is readable and writable
  // by the current process.
  if (!is_device(options.devnode)) {
    return absl::InvalidArgumentError(fmt::format(
        "User input device '{}' is not a writable character device file.",
        options.devnode));
  }
  if (options.name.size() > UINPUT_MAX_NAME_SIZE) {
    return absl::InvalidArgumentError(fmt::format(
        "User input device name '{}' must not be longer than {} characters.",
        options.name, UINPUT_MAX_NAME_SIZE));
  }

  // Read-write, non-blocking file descriptor to the uinput device node.
  int fd = ::open(options.devnode.c_str(), O_RDWR | O_CLOEXEC | O_NONBLOCK, 0);
  if (fd < 0) {
    return absl::ErrnoToStatus(errno, "Open user input device failed");
  }
  UserInputDevice result;
  result.fd_ = toolbelt::FileDescriptor(fd);
  result.info_ = options.info;
  result.name_ = options.name;
  result.devnode_ = options.devnode;

  // Set phys name
  if (ioctl(fd, UI_SET_PHYS, options.phys.c_str()) < 0) {
    return absl::ErrnoToStatus(
        errno, "Setting user input device physical path failed");
  }
  result.phys_ = options.phys;

  // Set properties
  for (const auto& prop : options.input_props) {
    if (ioctl(fd, UI_SET_PROPBIT, prop.code) < 0) {
      return absl::ErrnoToStatus(errno,
                                 "Setting user input device property failed");
    }
  }
  result.input_props_ = options.input_props;

  if (auto st = EnableEventCodes(fd, EV_KEY, options.capabilities.keys);
      !st.ok()) {
    return st;
  }
  if (auto st =
          EnableEventCodes(fd, EV_ABS, options.capabilities.absolute_axes);
      !st.ok()) {
    return st;
  }
  if (auto st =
          EnableEventCodes(fd, EV_REL, options.capabilities.relative_axes);
      !st.ok()) {
    return st;
  }
  if (auto st = EnableEventCodes(fd, EV_MSC, options.capabilities.miscs);
      !st.ok()) {
    return st;
  }
  if (auto st = EnableEventCodes(fd, EV_SW, options.capabilities.switches);
      !st.ok()) {
    return st;
  }
  if (auto st =
          EnableEventCodes(fd, EV_FF, options.capabilities.force_feedbacks);
      !st.ok()) {
    return st;
  }
  if (auto st = EnableEventCodes(fd, EV_SND, options.capabilities.sounds);
      !st.ok()) {
    return st;
  }
  result.capabilities_ = options.capabilities;

  if (auto st = result.Setup(options.max_effects); !st.ok()) {
    return st;
  }

  // Create the uinput device.
  if (ioctl(fd, UI_DEV_CREATE) < 0) {
    return absl::ErrnoToStatus(errno, "Failed to create user input device");
  }

  // An `InputDevice` for the fake input device. It's OK if the device cannot be
  // opened for reading and writing.
  if (auto dev_or = FindDevice(fd, result.name_); dev_or.ok()) {
    result.device_ = std::move(*dev_or);
  }

  return result;
}

absl::StatusOr<UserInputDevice> UserInputDevice::CreateFromDevices(
    const std::vector<InputDevice>& devices,
    const absl::flat_hash_set<std::uint16_t>& excluded_event_types,
    const CreateOptions& options) {
  CreateOptions new_options = options;
  new_options.capabilities = CapabilitiesInfo{};
  for (const auto& dev : devices) {
    new_options.max_effects =
        std::min(new_options.max_effects, dev.FFEffectsCount());
    MergeIfNotExcluded(EV_KEY, excluded_event_types,
                       new_options.capabilities.keys, dev.Capabilities().keys);
    MergeIfNotExcluded(EV_SYN, excluded_event_types,
                       new_options.capabilities.synchs,
                       dev.Capabilities().synchs);
    MergeIfNotExcluded(EV_REL, excluded_event_types,
                       new_options.capabilities.relative_axes,
                       dev.Capabilities().relative_axes);
    MergeIfNotExcluded(EV_ABS, excluded_event_types,
                       new_options.capabilities.absolute_axes,
                       dev.Capabilities().absolute_axes);
    MergeIfNotExcluded(EV_MSC, excluded_event_types,
                       new_options.capabilities.miscs,
                       dev.Capabilities().miscs);
    MergeIfNotExcluded(EV_SW, excluded_event_types,
                       new_options.capabilities.switches,
                       dev.Capabilities().switches);
    MergeIfNotExcluded(EV_LED, excluded_event_types,
                       new_options.capabilities.leds, dev.Capabilities().leds);
    MergeIfNotExcluded(EV_SND, excluded_event_types,
                       new_options.capabilities.sounds,
                       dev.Capabilities().sounds);
    MergeIfNotExcluded(EV_REP, excluded_event_types,
                       new_options.capabilities.autorepeats,
                       dev.Capabilities().autorepeats);
    MergeIfNotExcluded(EV_FF, excluded_event_types,
                       new_options.capabilities.force_feedbacks,
                       dev.Capabilities().force_feedbacks);
    MergeIfNotExcluded(EV_UINPUT, excluded_event_types,
                       new_options.capabilities.uinputs,
                       dev.Capabilities().uinputs);
  }

  return Create(new_options);
}

absl::StatusOr<UserInputDevice> UserInputDevice::CreateFromDevices(
    const std::vector<std::string>& device_filenames,
    const absl::flat_hash_set<std::uint16_t>& excluded_event_types,
    const CreateOptions& options) {
  std::vector<InputDevice> devices;
  devices.reserve(device_filenames.size());
  for (const auto& dev_path : device_filenames) {
    if (auto dev_or = InputDevice::Open(dev_path); dev_or.ok()) {
      devices.emplace_back(std::move(*dev_or));
    }
  }
  return CreateFromDevices(devices, excluded_event_types, options);
}

absl::Status UserInputDevice::Close() {
  if (ioctl(fd_.Fd(), UI_DEV_DESTROY) < 0) {
    int oerrno = errno;
    fd_.Close();
    return absl::ErrnoToStatus(oerrno, "Failed to close user input device");
  }
  fd_.Close();
  return absl::OkStatus();
}

absl::StatusOr<UInputUpload> UserInputDevice::BeginUpload(
    std::uint32_t request_id) const {
  uinput_ff_upload upload{};
  upload.request_id = request_id;
  if (ioctl(fd_.Fd(), UI_BEGIN_FF_UPLOAD, &upload) < 0) {
    return absl::ErrnoToStatus(errno, "Failed to begin uinput upload.");
  }
  UInputUpload result{};
  result.request_id = upload.request_id;
  result.retval = upload.retval;
  result.effect = AnyEffect::FromData(&upload.effect);
  result.old = AnyEffect::FromData(&upload.old);
  return result;
}

absl::Status UserInputDevice::EndUpload(const UInputUpload& upload) const {
  uinput_ff_upload to_upload{};
  to_upload.request_id = upload.request_id;
  to_upload.retval = upload.retval;
  upload.effect.ToData(&to_upload.effect);
  upload.old.ToData(&to_upload.old);
  if (ioctl(fd_.Fd(), UI_END_FF_UPLOAD, &to_upload) < 0) {
    return absl::ErrnoToStatus(errno, "Failed to end uinput upload.");
  }
  return absl::OkStatus();
}

absl::StatusOr<UInputErase> UserInputDevice::BeginErase(
    std::uint32_t effect_id) const {
  uinput_ff_erase erase{};
  erase.effect_id = effect_id;
  if (ioctl(fd_.Fd(), UI_BEGIN_FF_ERASE, &erase) < 0) {
    return absl::ErrnoToStatus(errno, "Failed to begin uinput erase.");
  }
  UInputErase result{};
  result.request_id = erase.request_id;
  result.retval = erase.retval;
  result.effect_id = erase.effect_id;
  return result;
}

absl::Status UserInputDevice::EndErase(const UInputErase& erase) const {
  uinput_ff_erase to_erase{};
  to_erase.request_id = erase.request_id;
  to_erase.retval = erase.retval;
  to_erase.effect_id = erase.effect_id;
  if (ioctl(fd_.Fd(), UI_BEGIN_FF_ERASE, &to_erase) < 0) {
    return absl::ErrnoToStatus(errno, "Failed to end uinput erase.");
  }
  return absl::OkStatus();
}

}  // namespace evdevpp
