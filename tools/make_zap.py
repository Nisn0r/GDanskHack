import json, sys
src, dst = sys.argv[1], sys.argv[2]
d = json.load(open(src))

et = d['endpointTypes'][1]          # "Touchlink" endpoint: has Color Control + OTA client
et['id'] = 1
et['name'] = 'Light'
dt = {'code': 268, 'profileId': 260, 'label': 'LO-colortemperaturelight', 'name': 'LO-colortemperaturelight'}
et['deviceTypeRef'] = dt
et['deviceTypes'] = [dt]
et['deviceIdentifiers'] = [268]
et['deviceTypeName'] = dt['name']
et['deviceTypeCode'] = 268

def attr(c, name):
    for a in c['attributes']:
        if a['name'] == name:
            return a
    raise KeyError((c['name'], name))

def setattr_(c, name, value=None, storage=None, included=1):
    a = attr(c, name)
    a['included'] = included
    if value is not None: a['defaultValue'] = value
    if storage: a['storageOption'] = storage

keep = []
for c in et['clusters']:
    if c['code'] == 4096:          # ZLL Commissioning: not used
        continue
    n = c['name']
    if n == 'Basic':
        setattr_(c, 'power source', '0x01')                 # mains, single phase
        setattr_(c, 'generic device class', '0x00')         # Lighting
        setattr_(c, 'generic device type', '0x09')          # Generic indoor fixture (light panel)
        setattr_(c, 'manufacturer name', 'Nisnor')
        setattr_(c, 'model identifier', 'Surface lumineuse CCT')
        setattr_(c, 'application version', '0x04')
        setattr_(c, 'sw build id', '0.4.0')
        setattr_(c, 'date code', '20261020')
    elif n == 'On/off':
        setattr_(c, 'on/off', '0x01', 'NVM')           # factory default: ON
        setattr_(c, 'start up on off', '0xFF', 'NVM')       # 0xFF = previous
    elif n == 'Level Control':
        setattr_(c, 'current level', '0xFE', 'NVM')
        setattr_(c, 'on level', '0xFF')                      # 0xFF = "On" restores the previous level
        # Expose the 1..254 range the plugin clamps to
        # (SL_ZIGBEE_AF_PLUGIN_LEVEL_CONTROL_MINIMUM/MAXIMUM_LEVEL in the .slcp).
        tmpl = attr(c, 'on level')
        for code, name, value in ((2, 'min level', '0x01'), (3, 'max level', '0xFE')):
            if not any(a['code'] == code for a in c['attributes']):
                a = dict(tmpl, code=code, name=name, defaultValue=value,
                         storageOption='RAM', included=1, reportable=0)
                c['attributes'].append(a)
        c['attributes'].sort(key=lambda a: a['code'])
        setattr_(c, 'start up current level', '0xFF', 'NVM') # 0xFF = previous
    elif n == 'Color Control':
        temp_attrs = {
            'remaining time': '0x0000',
            'current x': '0x616B', 'current y': '0x607D',      # mandatory in cluster spec
            'color temperature': '0x00FA',                    # 250 mireds = 4000 K
            'color mode': '0x02', 'enhanced color mode': '0x02',  # 2 = color temperature
            'color control options': '0x00',
            'number of primaries': '0x00',
            'color capabilities': '0x0010',                   # bit 4: color temperature only
            'color temp physical min': '0x0099',              # 153 mireds = 6500 K
            'color temp physical max': '0x0172',              # 370 mireds = 2700 K
            'couple color temp to level min-mireds': '0x0099',
            'start up color temperature mireds': '0xFFFF',    # 0xFFFF = previous
            'cluster revision': '0x0001',
        }
        for a in c['attributes']:
            if a['name'] in temp_attrs:
                a['included'] = 1
                a['defaultValue'] = temp_attrs[a['name']]
            else:
                a['included'] = 0
        attr(c, 'color temperature')['storageOption'] = 'NVM'
        attr(c, 'start up color temperature mireds')['storageOption'] = 'NVM'
        for cmd in c['commands']:
            cmd['isEnabled'] = int(cmd['name'] in (
                'MoveToColorTemperature', 'MoveColorTemperature',
                'StepColorTemperature', 'StopMoveStep'))
    keep.append(c)
et['clusters'] = keep

d['endpointTypes'] = [et]
d['endpoints'] = [{'endpointTypeName': 'Light', 'endpointTypeIndex': 0,
                   'profileId': 260, 'endpointId': 1, 'networkId': 0}]
json.dump(d, open(dst, 'w'), indent=2)
print('clusters:', [(c['name'], c['side']) for c in keep])
