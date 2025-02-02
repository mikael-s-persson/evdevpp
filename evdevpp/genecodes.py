"""
Generate a Python extension module with the constants defined in linux/input.h.
"""

import os, sys, re


# -----------------------------------------------------------------------------
# The default header file locations to try.
headers = [
  "/usr/include/linux/input.h",
  "/usr/include/linux/input-event-codes.h",
  "/usr/include/linux/uinput.h",
]

if len(sys.argv) > 3:
  headers = sys.argv[1:-2]

hdr_fname = sys.argv[-2]
src_fname = sys.argv[-1]

def make_header_for_class(classname, code_lines):
  hdr_template = r"""
struct %s {
  std::uint16_t code = 0;
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr %s(std::uint16_t init_code = 0) : code(init_code) {}
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr operator std::uint16_t() const { return code; }
%s
  static const absl::flat_hash_map<std::uint16_t, const char*>& CodeToString();
  [[nodiscard]] const char* ToString() const {
    const auto& m = CodeToString();
    auto it = m.find(code);
    if (it == m.end()) {
      return "UNKNOWN";
    }
    return it->second;
  }
  static constexpr const char* kClassName = "%s";
};"""
  return hdr_template % (classname, classname, code_lines, classname)


def make_source_for_class(classname, val_lines, str_lines):
  src_template = r"""
%s

const absl::flat_hash_map<std::uint16_t, const char*>& %s::CodeToString() {
  static const auto* code_to_str = new absl::flat_hash_map<std::uint16_t, const char*>{[]() {
    absl::flat_hash_map<std::uint16_t, const char*> result;
%s
    return result;
  }()};
  return *code_to_str;
}"""
  return src_template % (val_lines, classname, str_lines)

# -----------------------------------------------------------------------------
all_classes = {
  "KEY": ("Key", [], [], []),
  "ABS": ("AbsoluteAxis", [], [], []),
  "REL": ("RelativeAxis", [], [], []),
  "SW": ("Switch", [], [], []),
  "MSC": ("Misc", [], [], []),
  "LED": ("LED", [], [], []),
  "BTN": ("Button", [], [], []),
  "REP": ("Autorepeat", [], [], []),
  "SND": ("Sound", [], [], []),
  "ID": ("ID", [], [], []),
  "EV": ("EventType", [], [], []),
  "BUS": ("BusType", [], [], []),
  "SYN": ("Synch", [], [], []),
  "FF": ("ForceFeedback", [], [], []),
  "UI_FF": ("UIForceFeedback", [], [], []),
  "INPUT_PROP": ("Property", [], [], []),
}
all_classes_key_regex = "|".join(all_classes.keys())

macro_regex = r"#define\s+(%s)_(\w+)\s+([xa-fA-F0-9]+)[\s\n]" % (all_classes_key_regex)
macro_regex = re.compile(macro_regex)

macro_alias_regex = r"#define\s+(%s)_(\w+)\s+(%s)_(\w+)[\s\n]" % (all_classes_key_regex, all_classes_key_regex)
macro_alias_regex = re.compile(macro_alias_regex)

# -----------------------------------------------------------------------------
hdr_file_template = r"""
// Automatically generated by genecodes
#ifndef EVDEVPP_EVDEVPP_%s_
#define EVDEVPP_EVDEVPP_%s_

#include <cstdint>
#include <string_view>

#include "absl/container/flat_hash_map.h"

namespace evdevpp {

%s

} // namespace evdevpp

#endif // EVDEVPP_EVDEVPP_%s_
"""

src_file_template = r"""
// Automatically generated by genecodes
#include "evdevpp/%s"

#include <cstdint>
#include <unordered_map>

namespace evdevpp {

%s

} // namespace evdevpp
"""


def to_camel_case_constant(upper_snake):
    result = "k"
    for w in upper_snake.split("_"):
        result += w[0] + w[1:].lower()
    return result


seen_cls_mem_pairs = {}

def parse_header(header):
    for line in open(header):
        macro = macro_regex.search(line)
        # Skip some exceptional defines.
        if macro and macro.group(1) == "EV" and macro.group(2) == "VERSION":
            continue
        if macro and macro.group(1) in all_classes:
            cls_name = all_classes[macro.group(1)][0]
            mem_name = to_camel_case_constant(macro.group(2))
            if (cls_name, mem_name) in seen_cls_mem_pairs:
                continue
            all_classes[macro.group(1)][1].append("  static const %s %s;" % (cls_name, mem_name))
            all_classes[macro.group(1)][2].append("const %s %s::%s = %s;" % (cls_name, cls_name, mem_name, macro.group(3)))
            all_classes[macro.group(1)][3].append(r"""    result[%s] = "%s::%s";""" % (mem_name, cls_name, mem_name))
            seen_cls_mem_pairs[(cls_name, mem_name)] = True
            continue
        macro_alias = macro_alias_regex.search(line)
        if macro_alias and macro_alias.group(1) in all_classes and macro_alias.group(3) == macro_alias.group(1):
            cls_name = all_classes[macro_alias.group(1)][0]
            mem_name = to_camel_case_constant(macro_alias.group(2))
            val_name = to_camel_case_constant(macro_alias.group(4))
            if (cls_name, mem_name) in seen_cls_mem_pairs:
                continue
            all_classes[macro_alias.group(1)][1].append("  static const %s %s;" % (cls_name, mem_name))
            all_classes[macro_alias.group(1)][2].append("const %s %s::%s = %s::%s;" % (cls_name, cls_name, mem_name, cls_name, val_name))
            seen_cls_mem_pairs[(cls_name, mem_name)] = True


for header in headers:
    try:
        fh = open(header)
    except (IOError, OSError):
        continue
    parse_header(header)

all_hdr_lines = []
all_src_lines = []
for k in all_classes.keys():
    all_hdr_lines.append(make_header_for_class(all_classes[k][0], os.linesep.join(all_classes[k][1])))
    all_src_lines.append(make_source_for_class(all_classes[k][0], os.linesep.join(all_classes[k][2]), os.linesep.join(all_classes[k][3])))

hdr_bname = os.path.basename(hdr_fname)
hdr_guard_name = hdr_bname.upper().replace(".", "_")

hdr_f = open(hdr_fname, "w")
hdr_f.write(hdr_file_template % (hdr_guard_name, hdr_guard_name, os.linesep.join(all_hdr_lines), hdr_guard_name))
hdr_f.close()

src_f = open(src_fname, "w")
src_f.write(src_file_template % (hdr_bname, os.linesep.join(all_src_lines)))
src_f.close()
