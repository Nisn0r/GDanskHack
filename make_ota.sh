#!/usr/bin/env bash
# Package the current app build as a Zigbee OTA file for the coordinator.
#
#   ./make_ota.sh  ->  out/gdansk_light-v<N>.gbl / .ota
#
# The OTA header (manufacturer / image type / firmware version) must match
# what the device announces, otherwise the OTA server never offers it:
#   - manufacturer id : SL_ZIGBEE_ZCL_MANUFACTURER_CODE (app/autogen/zap-config.h)
#   - image type      : SL_ZIGBEE_AF_PLUGIN_OTA_CLIENT_POLICY_IMAGE_TYPE_ID
#   - firmware version: SL_ZIGBEE_AF_PLUGIN_OTA_CLIENT_POLICY_FIRMWARE_VERSION
#                       (bump it in app/gdansk_light.slcp, regenerate, rebuild:
#                       the device only accepts a strictly newer version)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
APP="$HERE/app"
IMG="$APP/cmake_gcc/build/base/gdansk_light.s37"
OUT="$HERE/out"

cfg() { awk -v k="$1" '$1=="#define" && $2==k {print $3}' "$2"; }
MFG=$(cfg SL_ZIGBEE_ZCL_MANUFACTURER_CODE "$APP/autogen/zap-config.h")
TYPE=$(cfg SL_ZIGBEE_AF_PLUGIN_OTA_CLIENT_POLICY_IMAGE_TYPE_ID "$APP/config/ota-client-policy-config.h")
VER=$(cfg SL_ZIGBEE_AF_PLUGIN_OTA_CLIENT_POLICY_FIRMWARE_VERSION "$APP/config/ota-client-policy-config.h")
VER=$(printf '0x%08X' "$VER")

mkdir -p "$OUT"
GBL="$OUT/gdansk_light-$VER.gbl"
OTA="$OUT/gdansk_light-$VER.ota"

# Unsigned / unencrypted for now: the bootloader doesn't enforce signatures
# (see bootloader/README.md before enabling --sign/--encrypt).
commander gbl create "$GBL" --app "$IMG" --device EFR32MG21A020F768IM32
commander ota create --type zigbee -o "$OTA" --upgrade-image "$GBL" \
  --manufacturer-id "$MFG" --image-type "$TYPE" --firmware-version "$VER" \
  --string "gdansk-light $VER"

echo "OTA: $OTA (manufacturer $MFG, image type $TYPE, version $VER)"
