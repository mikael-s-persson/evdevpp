
#include "CLI/CLI.hpp"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/status/status.h"
#include "evdevpp/device.h"
#include "evdevpp/ecodes.h"
#include "evdevpp/events.h"
#include "evdevpp/info.h"
#include "fmt/core.h"

using namespace evdevpp;

template <typename EVCode>
void print_cap(const absl::flat_hash_set<std::uint16_t>& codes,
               EventType ev_type) {
  if (!codes.empty()) {
    fmt::print("  Event type {} (0x{:X})\n", ev_type.ToString(), ev_type.code);
    for (auto k : codes) {
      if (EVCode::CodeToString().contains(k)) {
        fmt::print("    Event code {} (0x{:X})\n", EVCode{k}.ToString(), k);
      }
    }
  }
}

int main(int argc, char** argv) {
  CLI::App cli_de{"Dump evdev input device events."};

  std::string arg_device_path = "/dev/input/event0";
  cli_de.add_option("-d,--device_path", arg_device_path, "Device path.")
      ->transform(CLI::EscapedString);

  bool arg_rumble = false;
  cli_de.add_flag("-r,--rumble", arg_rumble,
                  "Rumble device if read times out.");

  try {
    cli_de.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    return cli_de.exit(e);
  }

  absl::StatusOr<InputDevice> device_or = InputDevice::Open(arg_device_path);
  if (!device_or.ok()) {
    fmt::print(stderr, "Failed to open device: {}\n",
               device_or.status().ToString());
    return 1;
  }
  InputDevice device = std::move(*device_or);

  fmt::print("Input driver version is {}.{}.{}\n", (device.Version() >> 16),
             (device.Version() >> 8) % 256, device.Version() % 256);
  fmt::print(
      "Input device ID: bus 0x{:04X} vendor 0x{:04X} product 0x{:04X} version "
      "0x{:04X}\n",
      device.Info().bustype, device.Info().vendor, device.Info().product,
      device.Info().version);
  fmt::print("Input device name: '{}'\n", device.Name());

  const CapabilitiesInfo& capabs = device.Capabilities();
  fmt::print("Supported events:\n");
  print_cap<Synch>(capabs.synchs, EventType::kSyn);
  if (!capabs.keys.empty()) {
    fmt::print("  Event type {} (0x{:X})\n", EventType::kKey.ToString(),
               EventType::kKey.code);
    for (auto k : capabs.keys) {
      if (Key::CodeToString().contains(k)) {
        fmt::print("    Event code {} (0x{:X})\n", Key{k}.ToString(), k);
      } else if (Button::CodeToString().contains(k)) {
        fmt::print("    Event code {} (0x{:X})\n", Button{k}.ToString(), k);
      }
    }
  }
  print_cap<RelativeAxis>(capabs.relative_axes, EventType::kRel);
  if (!capabs.absolute_axes.empty()) {
    fmt::print("  Event type {} (0x{:X})\n", EventType::kAbs.ToString(),
               EventType::kAbs.code);
    for (auto [k, i] : capabs.absolute_axes) {
      if (AbsoluteAxis::CodeToString().contains(k)) {
        fmt::print("    Event code {} (0x{:X})\n", AbsoluteAxis{k}.ToString(),
                   k);
        fmt::print("      Value {:6d}\n", i.value);
        fmt::print("      Min   {:6d}\n", i.minimum);
        fmt::print("      Max   {:6d}\n", i.maximum);
        fmt::print("      Fuzz  {:6d}\n", i.fuzz);
        fmt::print("      Flat  {:6d}\n", i.flat);
      }
    }
  }
  print_cap<Misc>(capabs.miscs, EventType::kMsc);
  print_cap<Switch>(capabs.switches, EventType::kSw);
  print_cap<LED>(capabs.leds, EventType::kLed);
  print_cap<Sound>(capabs.sounds, EventType::kSnd);
  print_cap<Autorepeat>(capabs.autorepeats, EventType::kRep);
  print_cap<ForceFeedback>(capabs.force_feedbacks, EventType::kFf);
  print_cap<UIForceFeedback>(capabs.uinputs, EventType::kUinput);

  while (true) {
    auto wait_res_or = device.Wait(absl::Seconds(5));
    if (!wait_res_or.ok()) {
      fmt::print(stderr, "Failed to wait for events on device: {}\n",
                 wait_res_or.status().ToString());
      return 2;
    }
    if (!*wait_res_or) {
      if (arg_rumble &&
          capabs.force_feedbacks.contains(ForceFeedback::kPeriodic) &&
          capabs.force_feedbacks.contains(ForceFeedback::kSquare)) {
        fmt::print("Rumbling device {} ...\n", arg_device_path);
        PeriodicEffect eff{};
        eff.waveform = ForceFeedback::kSquare;
        eff.period = absl::Milliseconds(500);
        eff.magnitude = 30000;
        auto eff_id_or = device.NewEffect(AnyEffect{eff});
        if (!eff_id_or.ok()) {
          fmt::print(stderr, "Failed to upload rumble effect on device: {}\n",
                     eff_id_or.status().ToString());
          return 4;
        }
        if (auto st = device.Write(EventType::kFf, *eff_id_or, 1); !st.ok()) {
          fmt::print(stderr, "Failed to play rumble effect on device: {}\n",
                     st.ToString());
          return 5;
        }
        absl::SleepFor(absl::Milliseconds(600));
        if (auto st = device.EraseEffect(*eff_id_or); !st.ok()) {
          fmt::print(stderr, "Failed to erase rumble effect on device: {}\n",
                     st.ToString());
          return 6;
        }
      } else {
        fmt::print("Waiting for events on device {} ...\n", arg_device_path);
      }
      continue;
    }
    absl::StatusOr<std::vector<InputEvent>> events_or = device.ReadAll();
    if (!events_or.ok()) {
      fmt::print(stderr, "Failed to read events on device: {}\n",
                 events_or.status().ToString());
      return 3;
    }
    for (auto event : *events_or) {
      const AnyInputEvent categorized_event = AnyInputEvent::Categorize(event);
      fmt::print("{}\n", categorized_event);
    }
  }
}
