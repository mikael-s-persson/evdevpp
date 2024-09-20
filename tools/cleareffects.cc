
#include "CLI/CLI.hpp"
#include "absl/status/statusor.h"
#include "absl/status/status.h"
#include "evdevpp/device.h"
#include "fmt/core.h"

int main(int argc, char** argv) {
  CLI::App cli_de{"Clear effects on input device."};

  std::string arg_device_path = "/dev/input/event0";
  cli_de.add_option("-d,--device_path", arg_device_path, "Device path.")
      ->transform(CLI::EscapedString);

  try {
    cli_de.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    return cli_de.exit(e);
  }

  absl::StatusOr<evdevpp::InputDevice> device_or = evdevpp::InputDevice::Open(arg_device_path);
  if (!device_or.ok()) {
    fmt::print(stderr, "Failed to open device: {}\n",
               device_or.status().ToString());
    return 1;
  }
  evdevpp::InputDevice device = std::move(*device_or);
  if (auto st = device.Grab(); !st.ok()) {
    fmt::print(stderr, "Failed to grab device: {}\n", st.ToString());
    return 2;
  }
  device.ClearEffects();
  if (auto st = device.Ungrab(); !st.ok()) {
    fmt::print(stderr, "Failed to ungrab device: {}\n", st.ToString());
    return 3;
  }
  return 0;
}
