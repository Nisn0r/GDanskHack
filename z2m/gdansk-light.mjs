// Zigbee2MQTT external converter for the gdansk-light firmware
// (INSPIRE gdansk light panel, white CCT, ref. 84845491, with a reflashed
// Tuya ZSLC5 module). See README.md in this folder.
//
// The firmware only uses standard ZCL clusters (On/Off, Level Control,
// Color Control in colour-temperature mode, OTA Upgrade), so modern extends
// cover everything.

import * as m from "zigbee-herdsman-converters/lib/modernExtend";

export default {
    // ModelIdentifier reported by the lamp (Basic cluster)
    zigbeeModel: ["Surface lumineuse CCT"],
    model: "GDANSK-CCT",
    vendor: "Nisnor",
    description: "INSPIRE gdansk CCT light panel (reflashed Tuya ZSLC5 module)",
    extend: [
        m.identify(),
        m.light({
            // 153-370 mireds = 6500-2700 K; startup = StartUpColorTemperatureMireds
            colorTemp: {range: [153, 370], startup: true},
            // StartUpOnOff / StartUpCurrentLevel
            powerOnBehavior: true,
            // Identify "Trigger Effect" is not implemented by the firmware
            effect: false,
            // Bind + report onOff, currentLevel, colorTemperature (3 entries;
            // the firmware has room for 10 bindings and 5 reports)
            configureReporting: true,
            ota: true,
        }),
    ],
};
