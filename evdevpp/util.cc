#include "evdevpp/util.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "evdevpp/ecodes.h"
#include "evdevpp/events.h"

namespace evdevpp {

namespace {
bool is_device(const std::filesystem::file_status& f_stat) {
  return (f_stat.type() == std::filesystem::file_type::character &&
          (f_stat.permissions() & std::filesystem::perms::others_read) !=
              std::filesystem::perms::none &&
          (f_stat.permissions() & std::filesystem::perms::others_write) !=
              std::filesystem::perms::none);
}
}  // namespace

// List readable character devices in `input_device_dir`.
std::vector<std::string> list_devices(std::string_view input_device_dir) {
  std::vector<std::string> result;
  for (const auto& dev_filename :
       std::filesystem::directory_iterator(input_device_dir)) {
    if (std::string_view{dev_filename.path().filename().native()}.substr(
            0, 5) != "event" ||
        !is_device(dev_filename.status())) {
      continue;
    }
    result.emplace_back(dev_filename.path());
  }
  return result;  // NRVO
}

bool is_device(const std::string& filename) {
  return is_device(std::filesystem::status(filename));
}

}  // namespace evdevpp
