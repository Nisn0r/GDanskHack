#!/usr/bin/env bash
# Set the firmware version everywhere it lives, so an OTA image is offered:
#   tools/set_version.sh 2      ->  version 2, SWBuildID "0.2.0"
# Then: REGEN=1 ./build.sh app && ./make_ota.sh
#
# slc does not overwrite existing config/*.h on regeneration, hence the
# header edits next to the .slcp one.
set -euo pipefail
N="${1:?version number}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
HEX=$(printf '0x%08X' "$N")
APP="$HERE/app"

# OTA file version (what the OTA server compares)
sed -i -E "/SL_ZIGBEE_AF_PLUGIN_OTA_CLIENT_POLICY_FIRMWARE_VERSION/{n;s/value: .*/value: $HEX/}" "$APP/gdansk_light.slcp"
sed -i -E "s/^(#define SL_ZIGBEE_AF_PLUGIN_OTA_CLIENT_POLICY_FIRMWARE_VERSION +).*/\1$N/" \
  "$APP/config/ota-client-policy-config.h"
# Application properties (GBL / bootloader)
sed -i -E "s/^(#define SL_APPLICATION_VERSION +).*/\1$N/" "$APP/config/app_properties_config.h"
# Basic cluster ApplicationVersion / SWBuildID
sed -i -E "s/(setattr_\(c, 'application version', )'[^']*'/\1'$(printf '0x%02X' "$N")'/; \
           s/(setattr_\(c, 'sw build id', )'[^']*'/\1'0.$N.0'/" "$HERE/tools/make_zap.py"
SDK="$("${SLT:-slt}" where simplicity-sdk)"
python3 -I "$HERE/tools/make_zap.py" \
  "$SDK/zigbee_app/z3/zigbee_z3_light/config/zcl/zcl_config.zap" \
  "$APP/config/zcl/zcl_config.zap" > /dev/null

grep -H -E "POLICY_FIRMWARE_VERSION +|SL_APPLICATION_VERSION +" \
  "$APP/config/ota-client-policy-config.h" "$APP/config/app_properties_config.h"
grep -A1 "POLICY_FIRMWARE_VERSION" "$APP/gdansk_light.slcp" | tail -1
grep -E "application version|sw build id" "$HERE/tools/make_zap.py"
