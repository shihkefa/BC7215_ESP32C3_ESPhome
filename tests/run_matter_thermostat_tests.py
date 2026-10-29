"""Check Room AC generation and the pinned SDK's actual SystemMode validation.

No device is accessed. SDK getter/storage boundaries are stubbed; the enum
definitions and SystemMode rejection rules are copied directly from that SDK.
"""
from pathlib import Path
from types import SimpleNamespace
import argparse
import ast
import subprocess

p = argparse.ArgumentParser()
p.add_argument("--sdk", type=Path, required=True, help="esp_matter component directory")
p.add_argument("--cxx", required=True)
p.add_argument("--build-dir", type=Path, required=True)
a = p.parse_args()
b = a.build_dir.resolve()
b.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[1]

generator_ast = ast.parse((root / "components/matter/endpoints.py").read_text(encoding="utf-8"))
generator = next(n for n in ast.walk(generator_ast) if isinstance(n, ast.FunctionDef)
                 and n.name == "_make_device_type_lines")
namespace = {"DeviceType": object, "CONF_MIN_LEVEL": "min_level", "CONF_MAX_LEVEL": "max_level"}
exec(compile(ast.Module(body=[generator], type_ignores=[]), "endpoints.py", "exec"), namespace)
device = SimpleNamespace(namespace="room_air_conditioner", name="room_air_conditioner",
                         conf_key="room_air_conditioner", server_clusters=[])
for heating, cooling, expected in [(True, True, 4), (True, False, 2), (False, True, 0)]:
    owner = SimpleNamespace(_config={device.conf_key: {}}, _cluster_configs={
        "Thermostat": SimpleNamespace(enabled_features={"Heating": heating, "Cooling": cooling})})
    emitted = "\n".join(namespace["_make_device_type_lines"](owner, device, 0))
    assert f"thermostat.control_sequence_of_operation = {expected};" in emitted
    assert emitted.index("control_sequence_of_operation") < emitted.index("::add(endpoint")

sdk = a.sdk.resolve()
chip = sdk.parents[1] / "connectedhomeip/connectedhomeip"
enums_source = (chip / "zzz_generated/app-common/clusters/Thermostat/Enums.h").read_text(encoding="utf-8")
validator_source = (chip / "src/app/clusters/thermostat-server/ThermostatCluster.cpp").read_text(encoding="utf-8")
sdk_config = (sdk / "data_model/generated/clusters/thermostat/thermostat.h").read_text(encoding="utf-8")
assert "control_sequence_of_operation(0)" in sdk_config

def braced(source, start):
    opening = source.index("{", start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

enum_blocks = []
for name in ["ControlSequenceOfOperationEnum", "SystemModeEnum"]:
    start = enums_source.index("enum class " + name)
    enum_blocks.append(braced(enums_source, start) + ";")
case_start = validator_source.rindex("case SystemMode::Id: {")
case = braced(validator_source, case_start)
case_body = case[case.index("{") + 1:-1]

yaml = (root / "bc7215a-c3.yaml").read_text(encoding="utf-8")
boot = yaml[yaml.index("  on_boot:"):yaml.index("  on_shutdown:")]
assert "path: matter_ac.thermostat.control_sequence_of_operation\n          value: 4" in boot
assert boot.index("control_sequence_of_operation") < boot.index("path: matter_ac.thermostat.system_mode")

pre = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
enum class Status { Success, InvalidValue };
'''
support = r'''
template <typename T> T EnsureKnownEnumValue(T value) { return value; }
ControlSequenceOfOperationEnum saved_sequence=ControlSequenceOfOperationEnum::kCoolingOnly;
namespace ControlSequenceOfOperation {
Status Get(uint16_t, ControlSequenceOfOperationEnum *out) { *out=saved_sequence; return Status::Success; }
}
Status validate(uint8_t desired) { uint16_t endpoint=1; uint8_t *value=&desired;
'''
test = r'''
}
int main() {
  // Reproduce v1.0.32's default: Cooling feature works, Heating is rejected
  // despite its advertised feature flag and ESPHome mode callback.
  assert(validate(static_cast<uint8_t>(SystemModeEnum::kCool))==Status::Success);
  assert(validate(static_cast<uint8_t>(SystemModeEnum::kHeat))==Status::InvalidValue);
  // Both fresh generated config and the on_boot correction select this value.
  saved_sequence=static_cast<ControlSequenceOfOperationEnum>(4);
  for (auto mode : {SystemModeEnum::kOff, SystemModeEnum::kAuto, SystemModeEnum::kCool,
                    SystemModeEnum::kHeat, SystemModeEnum::kFanOnly, SystemModeEnum::kDry})
    assert(validate(static_cast<uint8_t>(mode))==Status::Success);
  // An upgraded device must correct the restored NONVOLATILE CoolingOnly
  // value before reporting its previously saved Heat state.
  saved_sequence=ControlSequenceOfOperationEnum::kCoolingOnly;
  assert(validate(static_cast<uint8_t>(SystemModeEnum::kHeat))==Status::InvalidValue);
  saved_sequence=ControlSequenceOfOperationEnum::kCoolingAndHeating;
  assert(validate(static_cast<uint8_t>(SystemModeEnum::kHeat))==Status::Success);
  std::cout << "PASS locked SDK thermostat: reproduce CoolingOnly Heat rejection; heat/cool/auto/off/fan/dry after correction; generated config and upgrade boot order\n";
}
'''
source = b / "thermostat.cpp"
source.write_text(pre + "\n".join(enum_blocks) + support + case_body + test, encoding="utf-8")
exe = b / "thermostat.exe"
subprocess.run([a.cxx, "c++", "-std=c++17", str(source), "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
