#include "evdevpp/device.h"

#include <fcntl.h>

#include <array>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

#include "evdevpp/info.h"
#include "linux/input.h"

namespace evdevpp {

namespace {

template <typename... Args>
int VarTempIOCTL(int fd, std::uint64_t req, Args&&... args) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) POSIX API
  return ioctl(fd, req, std::forward<Args>(args)...);
}

bool IsDevice(const std::filesystem::file_status& f_stat) {
  return (f_stat.type() == std::filesystem::file_type::character &&
          (f_stat.permissions() & std::filesystem::perms::group_read) !=
              std::filesystem::perms::none &&
          (f_stat.permissions() & std::filesystem::perms::group_write) !=
              std::filesystem::perms::none);
}

template <std::size_t N>
bool IsBitSet(const std::array<std::uint8_t, N>& bitmask, int bit) {
  return (bitmask[bit / 8] & (1 << (bit % 8))) != 0;
}

}  // namespace

// List readable character devices in `input_device_dir`.
std::vector<std::string> ListDevices(std::string_view input_device_dir) {
  std::vector<std::string> result;
  if (!std::filesystem::exists(input_device_dir)) {
    return result;  // NRVO
  }
  for (const auto& dev_filename :
       std::filesystem::directory_iterator(input_device_dir)) {
    if (std::string_view{dev_filename.path().filename().native()}.substr(
            0, 5) != "event" ||
        !IsDevice(dev_filename.status())) {
      continue;
    }
    result.emplace_back(dev_filename.path());
  }
  return result;  // NRVO
}

bool IsDevice(const std::string& filename) {
  return IsDevice(std::filesystem::status(filename));
}

absl::StatusOr<CapabilitiesInfo> GetCapabilities(int fd) {
  std::array<std::uint8_t, EV_MAX / 8 + 1> ev_bits{};
  if (VarTempIOCTL(fd, EVIOCGBIT(0, ev_bits.size()), ev_bits.data()) < 0) {
    return absl::ErrnoToStatus(errno,
                               "Getting capabilities of input device failed");
  }

  // Build a dictionary of the device's capabilities
  CapabilitiesInfo capabilities;
  for (std::uint16_t ev_type = 0; ev_type < EV_MAX; ++ev_type) {
    if (!IsBitSet(ev_bits, ev_type)) {
      continue;
    }

    std::array<std::uint8_t, KEY_MAX / 8 + 1> code_bits{};
    if (VarTempIOCTL(fd, EVIOCGBIT(ev_type, code_bits.size()),
                     code_bits.data()) < 0) {
      continue;
    }

    for (std::uint16_t ev_code = 0; ev_code < KEY_MAX; ++ev_code) {
      if (!IsBitSet(code_bits, ev_code)) {
        continue;
      }
      switch (ev_type) {
        case EV_ABS: {
          // Get abs{min,max,fuzz,flat} values for ABS_* event codes
          input_absinfo absinfo{};
          if (VarTempIOCTL(fd, EVIOCGABS(ev_code), &absinfo) < 0) {
            continue;
          }
          capabilities.absolute_axes.emplace(
              ev_code, AbsInfo{absinfo.value, absinfo.minimum, absinfo.maximum,
                               absinfo.fuzz, absinfo.flat, absinfo.resolution});
          break;
        }
        case EV_SYN:
          capabilities.synchs.insert(ev_code);
          break;
        case EV_KEY:
          capabilities.keys.insert(ev_code);
          break;
        case EV_REL:
          capabilities.relative_axes.insert(ev_code);
          break;
        case EV_MSC:
          capabilities.miscs.insert(ev_code);
          break;
        case EV_SW:
          capabilities.switches.insert(ev_code);
          break;
        case EV_LED:
          capabilities.leds.insert(ev_code);
          break;
        case EV_SND:
          capabilities.sounds.insert(ev_code);
          break;
        case EV_REP:
          capabilities.autorepeats.insert(ev_code);
          break;
        case EV_FF_STATUS:
          [[fallthrough]];
        case EV_FF:
          capabilities.force_feedbacks.insert(ev_code);
          break;
        case EV_PWR:
          [[fallthrough]];
        default:
          break;
      }
    }
  }
  return capabilities;
}

absl::StatusOr<InputDevice> InputDevice::Open(const std::string& dev_path) {
  int fd = ::open(dev_path.c_str(), O_RDWR | O_CLOEXEC | O_NONBLOCK, 0);
  if (fd < 0) {
    fd = ::open(dev_path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK, 0);
  }
  if (fd < 0) {
    return absl::ErrnoToStatus(errno, "Open input device failed");
  }
  InputDevice result;
  result.fd_ = toolbelt::FileDescriptor(fd);
  result.path_ = dev_path;

  input_id iid{};
  if (VarTempIOCTL(result.fd_.Fd(), EVIOCGID, &iid) < 0) {
    return absl::ErrnoToStatus(errno, "Input device info query failed");
  }
  result.info_.bustype = iid.bustype;
  result.info_.vendor = iid.vendor;
  result.info_.product = iid.product;
  result.info_.version = iid.version;

  result.name_.resize(256, '\0');
  if (VarTempIOCTL(result.fd_.Fd(), EVIOCGNAME(result.name_.size()),
                   result.name_.data()) < 0) {
    return absl::ErrnoToStatus(errno, "Input device name query failed");
  }
  result.name_.erase(result.name_.find('\0') + 1);

  // Some devices do not have a physical topology associated with them
  result.phys_.resize(256, '\0');
  (void)VarTempIOCTL(result.fd_.Fd(), EVIOCGPHYS(result.phys_.size()),
                     result.phys_.data());
  result.phys_.erase(result.phys_.find('\0') + 1);

  // Some kernels have started reporting bluetooth controller MACs as phys.
  // This lets us get the real physical address. As with phys, it may be blank.
  result.uniq_.resize(256, '\0');
  (void)VarTempIOCTL(result.fd_.Fd(), EVIOCGUNIQ(result.uniq_.size()),
                     result.uniq_.data());
  result.uniq_.erase(result.uniq_.find('\0') + 1);

  if (VarTempIOCTL(result.fd_.Fd(), EVIOCGVERSION, &result.version_) < 0) {
    return absl::ErrnoToStatus(errno,
                               "Input device protocol version query failed");
  }

  auto cap_or = GetCapabilities(result.fd_.Fd());
  if (!cap_or.ok()) {
    return cap_or.status();
  }
  result.capabilities_ = std::move(*cap_or);

  if (VarTempIOCTL(result.fd_.Fd(), EVIOCGEFFECTS, &result.ff_effects_count_) <
      0) {
    return absl::ErrnoToStatus(errno,
                               "Input device ff-effects count query failed");
  }

  return result;
}

absl::Status InputDevice::Grab() const {
  if (VarTempIOCTL(fd_.Fd(), EVIOCGRAB, 1) != 0) {
    return absl::ErrnoToStatus(errno, "Input device grabbing failed");
  }
  return absl::OkStatus();
}

absl::Status InputDevice::Ungrab() const {
  if (VarTempIOCTL(fd_.Fd(), EVIOCGRAB, 0) != 0) {
    return absl::ErrnoToStatus(errno, "Input device ungrabbing failed");
  }
  return absl::OkStatus();
}

absl::StatusOr<absl::flat_hash_set<std::uint16_t>> InputDevice::Properties()
    const {
  std::array<std::uint8_t, (INPUT_PROP_MAX + 7) / 8> bytes{};
  if (VarTempIOCTL(fd_.Fd(), EVIOCGPROP(bytes.size()), bytes.data()) < 0) {
    return absl::ErrnoToStatus(errno, "Input device properties query failed");
  }

  absl::flat_hash_set<std::uint16_t> result;
  for (int i = 0; i < INPUT_PROP_MAX; ++i) {
    if (IsBitSet(bytes, i)) {
      result.insert(i);
    }
  }

  return result;
}

absl::Status InputDevice::SetAbsoluteAxisInfo(AbsoluteAxis axis,
                                              const AbsInfo& abs_info) {
  input_absinfo absinfo = {
      .value = abs_info.value,
      .minimum = abs_info.minimum,
      .maximum = abs_info.maximum,
      .fuzz = abs_info.fuzz,
      .flat = abs_info.flat,
      .resolution = abs_info.resolution,
  };

  if (VarTempIOCTL(fd_.Fd(), EVIOCSABS(axis.code), &absinfo) == -1) {
    return absl::ErrnoToStatus(
        errno, "Input device setting absolute axis info failed");
  }

  capabilities_.absolute_axes[axis.code] = abs_info;
  return absl::OkStatus();
}

absl::StatusOr<absl::flat_hash_set<std::uint16_t>> InputDevice::GetActiveKeys()
    const {
  std::array<std::uint8_t, (KEY_MAX + 7) / 8> bytes{};
  if (VarTempIOCTL(fd_.Fd(), EVIOCGKEY(bytes.size()), bytes.data()) == -1) {
    return absl::ErrnoToStatus(errno,
                               "Input device getting active keys failed");
  }

  absl::flat_hash_set<std::uint16_t> result;
  for (int i = 0; i < KEY_MAX; ++i) {
    if (IsBitSet(bytes, i)) {
      result.insert(i);
    }
  }

  return result;
}

absl::StatusOr<KeyRepeatInfo> InputDevice::GetRepeat() const {
  std::array<unsigned int, 2> rep{};
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
  if (VarTempIOCTL(fd_.Fd(), EVIOCGREP, &rep) == -1) {
    return absl::ErrnoToStatus(errno, "Input device getting key-repeat failed");
  }
  return KeyRepeatInfo{.repeat_key_per_s = rep[0],
                       .delay = absl::Milliseconds(rep[1])};
}

absl::Status InputDevice::SetRepeat(const KeyRepeatInfo& rep_info) const {
  std::array<unsigned int, 2> rep = {
      rep_info.repeat_key_per_s,
      static_cast<unsigned int>(absl::ToInt64Milliseconds(rep_info.delay))};
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
  if (VarTempIOCTL(fd_.Fd(), EVIOCSREP, &rep) == -1) {
    return absl::ErrnoToStatus(errno, "Input device setting key-repeat failed");
  }

  return absl::OkStatus();
}

absl::StatusOr<absl::flat_hash_set<std::uint16_t>> InputDevice::LEDs() const {
  std::array<std::uint8_t, (LED_MAX + 7) / 8> bytes{};
  if (VarTempIOCTL(fd_.Fd(), EVIOCGLED(bytes.size()), bytes.data()) == -1) {
    return absl::ErrnoToStatus(errno,
                               "Input device getting active LEDs failed");
  }

  absl::flat_hash_set<std::uint16_t> result;
  for (int i = 0; i < LED_MAX; ++i) {
    if (IsBitSet(bytes, i)) {
      result.insert(i);
    }
  }

  return result;
}

absl::StatusOr<std::int16_t> InputDevice::NewEffect(
    const AnyEffect& new_effect) const {
  ff_effect effect{};
  new_effect.Base().ToData(static_cast<void*>(&effect));
  effect.id = -1;
  if (VarTempIOCTL(fd_.Fd(), EVIOCSFF, &effect) != 0) {
    return absl::ErrnoToStatus(errno, "Input device uploading effect failed");
  }
  return effect.id;
}

absl::Status InputDevice::UpdateEffect(
      const AnyEffect& updated_effect) const {
  ff_effect effect{};
  updated_effect.Base().ToData(static_cast<void*>(&effect));
  if (VarTempIOCTL(fd_.Fd(), EVIOCSFF, &effect) != 0) {
    return absl::ErrnoToStatus(errno, "Input device uploading effect failed");
  }
  return absl::OkStatus();
}

absl::Status InputDevice::EraseEffect(int id) const {
  if (VarTempIOCTL(fd_.Fd(), EVIOCRMFF, id) != 0) {
    return absl::ErrnoToStatus(errno, "Input device erasing effect failed");
  }
  return absl::OkStatus();
}

void InputDevice::ClearEffects() const {
  for (int i = 0; i < ff_effects_count_; ++i) {
    (void)EraseEffect(i);
  }
}

}  // namespace evdevpp
