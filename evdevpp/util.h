#ifndef EVDEVPP_EVDEVPP_UTIL_H_
#define EVDEVPP_EVDEVPP_UTIL_H_

#include <string>
#include <string_view>
#include <vector>

#include "evdevpp/ecodes.h"
#include "evdevpp/events.h"

namespace evdevpp {

// List readable character devices in `input_device_dir`.
std::vector<std::string> list_devices(
    std::string_view input_device_dir = "/dev/input");

// Check if `filename` is a readable and writable character device.
bool is_device(const std::string& filename);

}  // namespace evdevpp

#endif  // EVDEVPP_EVDEVPP_UTIL_H_
