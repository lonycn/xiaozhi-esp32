#!/usr/bin/env python3
"""Offline, fail-closed packaging for the bread compact S3 AI release variant.

This tool never builds, connects to a device, reads device flash, or publishes.
Inputs must be produced by a separate clean Git worktree. First-flash output
contains blank NVS and is appropriate only for a new/provisioned device.
"""
import argparse
import hashlib
import ipaddress
import json
import re
import struct
import subprocess
import sys
from pathlib import Path
from urllib.parse import urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parent))
from product_profiles import variant_sdkconfig

BOARD_TYPE = 'bread-compact-wifi'
BOARD_NAME = 'bread-compact-wifi-128x32-ai-release'
LAN_BOARD_NAME = 'bread-compact-wifi-128x32-ai-lan'
FLASH = 16 * 1024 * 1024
APP_OFFSET, APP_SIZE = 0x20000, 0x3f0000
ASSETS_OFFSET, ASSETS_SIZE = 0x800000, 0x800000
WAKE_WORDS = {'da leng zong guan', 'ni hao zong guan', 'zong guan zong guan'}
FILES = {'bootloader/bootloader.bin': 0, 'partition_table/partition-table.bin': 0x8000,
         'ota_data_initial.bin': 0xd000, 'xiaozhi.bin': APP_OFFSET,
         'generated_assets.bin': ASSETS_OFFSET}


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def version(text):
    require(isinstance(text, str) and re.fullmatch(r'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)', text) is not None, 'invalid version')
    return tuple(int(x) for x in text.split('.'))


def check_ota_url(value, development_lan=False):
    try:
        require(type(development_lan) is bool and isinstance(value, str)
                and not any(ord(ch) < 33 or ord(ch) == 127 for ch in value)
                and '\\' not in value, 'invalid OTA URL')
        p = urlsplit(value)
        require(bool(p.hostname) and not p.username and not p.password
                and not p.query and not p.fragment and not (p.hostname.lower() == 'tenclass.net'
                or p.hostname.lower().endswith('.tenclass.net')), 'invalid OTA URL')
        if p.scheme == 'https':
            require(p.port in (None, 443), 'invalid HTTPS port')
        elif development_lan and p.scheme == 'http':
            ip = ipaddress.IPv4Address(p.hostname)
            networks = ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16', '127.0.0.0/8')
            require(any(ip in ipaddress.IPv4Network(network) for network in networks)
                    and (p.port is None or 1 <= p.port <= 65535), 'non-local development address')
        else:
            raise ValueError('unsupported OTA scheme')
    except (ValueError, TypeError):
        raise ValueError('OTA URL requires HTTPS or explicit development-only LAN IPv4 HTTP') from None


def config_values(path):
    values = {}
    for line in path.read_text(encoding='utf-8').splitlines():
        if line.startswith('CONFIG_') and '=' in line:
            key, value = line.split('=', 1)
            require(key not in values, 'duplicate sdkconfig symbol')
            values[key] = value
    return values


def enabled(config, key):
    return config.get(key) == 'y'


def read_app(image):
    require(len(image) >= 288 and image[0] == 0xe9, 'invalid app image')
    require(struct.unpack_from('<H', image, 12)[0] == 9, 'app chip is not ESP32-S3')
    desc = image[32:288]
    require(struct.unpack_from('<I', desc)[0] == 0xabcd5432, 'missing app descriptor')
    def text(start, end):
        return desc[start:end].split(b'\0', 1)[0].decode('utf-8')
    return {'version': text(16, 48), 'project': text(48, 80), 'sdk': text(112, 144)}


def assets_files(image):
    require(12 <= len(image) <= ASSETS_SIZE, 'invalid assets size')
    count, checksum, size = struct.unpack_from('<III', image)
    require(0 < count <= 256 and size == len(image) - 12 and checksum == sum(image[12:]) & 0xffff,
            'invalid assets header or checksum')
    base = 12 + count * 44
    require(base <= len(image), 'truncated assets table')
    files, ranges = {}, []
    for i in range(count):
        start = 12 + i * 44
        name = image[start:start+32].split(b'\0', 1)[0].decode('utf-8')
        length, offset = struct.unpack_from('<II', image, start+32)
        pos = base + offset
        require(name and '/' not in name and name not in files and image[pos:pos+2] == b'ZZ'
                and pos + 2 + length <= len(image), 'invalid assets entry')
        require(all(pos + 2 + length <= x or pos >= y for x, y in ranges), 'overlapping assets')
        ranges.append((pos, pos+2+length))
        files[name] = image[pos+2:pos+2+length]
    return files


def check_assets(image):
    files = assets_files(image)
    require('index.json' in files and 'srmodels.bin' in files, 'missing wake assets')
    index = json.loads(files['index.json'])
    require(index.get('srmodels') == 'srmodels.bin', 'missing model index')
    model = index.get('multinet_model') or {}
    commands = model.get('commands') or []
    require(model.get('language') == 'cn' and isinstance(commands, list) and len(commands) == 3
            and all(isinstance(c, dict) and c.get('action') == 'wake' and c.get('text') == '大冷总管' for c in commands)
            and {c.get('command') for c in commands} == WAKE_WORDS,
            'missing three wake phrases')
    packed = files['srmodels.bin']
    require(len(packed) >= 4, 'truncated wake models')
    count = struct.unpack_from('<I', packed)[0]
    require(0 < count <= 32, 'invalid wake model count')
    offset = 4
    models = set()
    data_ranges = []
    for _ in range(count):
        require(offset + 36 <= len(packed), 'truncated wake model')
        name = packed[offset:offset+32].split(b'\0', 1)[0].decode('utf-8')
        number = struct.unpack_from('<I', packed, offset+32)[0]
        offset += 36
        require(0 < number <= 128 and offset + 40 * number <= len(packed), 'invalid wake model table')
        names = set()
        for _ in range(number):
            filename = packed[offset:offset+32].split(b'\0', 1)[0].decode('utf-8')
            pos, length = struct.unpack_from('<II', packed, offset+32)
            require(filename and filename not in names and length > 0 and pos + length <= len(packed), 'invalid wake model data')
            names.add(filename)
            data_ranges.append((pos, pos + length))
            offset += 40
        models.add(name)
    require(all(start >= offset and all(end <= other_start or start >= other_end
                for other_start, other_end in data_ranges[:i])
                for i, (start, end) in enumerate(data_ranges)), 'overlapping wake model data')
    require({'mn7_cn', 'fst'}.issubset(models), 'missing MultiNet7 or FST wake resources')


def check_partition_table(data):
    require(len(data) >= 32 * 6 and len(data) <= 0x1000, 'invalid partition table')
    entries = {}
    for pos in range(0, len(data)-31, 32):
        if data[pos:pos+2] != b'\xaa\x50':
            break
        _, typ, subtype, offset, size, name, _ = struct.unpack_from('<HBBII16sI', data, pos)
        name = name.split(b'\0', 1)[0].decode('ascii')
        require(name not in entries and offset + size <= FLASH, 'invalid partition entry')
        entries[name] = (typ, subtype, offset, size)
    expected = {'nvs': (1, 2, 0x9000, 0x4000), 'otadata': (1, 0, 0xd000, 0x2000),
                'phy_init': (1, 1, 0xf000, 0x1000), 'ota_0': (0, 0x10, APP_OFFSET, APP_SIZE),
                'ota_1': (0, 0x11, 0x410000, APP_SIZE), 'assets': (1, 0x82, ASSETS_OFFSET, ASSETS_SIZE)}
    require(entries == expected, 'unexpected flash partition layout')


def digest(data):
    return {'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data)}


def prepare_config(snapshot, output, ota_url, development_lan=False):
    """Generate an external config fragment from the tracked variant plus trusted URL."""
    snapshot, output = Path(snapshot).resolve(), Path(output).resolve()
    require(not output.exists() and snapshot not in output.parents, 'config output must be new and outside source snapshot')
    check_ota_url(ota_url, development_lan)
    board = json.loads((snapshot / 'main/boards/bread-compact-wifi/config.json').read_text(encoding='utf-8'))
    board_name = LAN_BOARD_NAME if development_lan else BOARD_NAME
    variants = [item for item in board.get('builds', []) if item.get('name') == board_name]
    require(board.get('type') == BOARD_TYPE and board.get('target') == 'esp32s3' and len(variants) == 1,
            'missing or ambiguous release variant')
    options = variant_sdkconfig(variants[0], snapshot)
    require(isinstance(options, list) and options.count('CONFIG_OTA_URL=""') == 1
            and all(isinstance(item, str) and re.fullmatch(r'CONFIG_[A-Z0-9_]+=[^\r\n]*', item) for item in options),
            'invalid release variant options')
    names = [item.split('=', 1)[0] for item in options]
    require(len(names) == len(set(names)), 'duplicate release variant options')
    output.parent.mkdir(parents=True, exist_ok=True)
    lines = ['CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI=y'] + [
        'CONFIG_OTA_URL=' + json.dumps(ota_url) if item.startswith('CONFIG_OTA_URL=') else item
        for item in options]
    lan_option = 'CONFIG_XIAOZHI_RELEASE_ALLOW_LAN_HTTP=' + ('y' if development_lan else 'n')
    lines = [lan_option if line.startswith('CONFIG_XIAOZHI_RELEASE_ALLOW_LAN_HTTP=') else line for line in lines]
    if not any(line.startswith('CONFIG_XIAOZHI_RELEASE_ALLOW_LAN_HTTP=') for line in lines):
        lines.append(lan_option)
    with output.open('x', encoding='utf-8') as target:
        target.write('\n'.join(lines) + '\n')


def package(snapshot, output, ota_url, app_version, previous_version, assets_version, development_lan=False):
    snapshot, output = Path(snapshot).resolve(), Path(output).resolve()
    require(not output.exists(), 'output already exists')
    require(snapshot not in output.parents, 'output must be outside source snapshot')
    check_ota_url(ota_url, development_lan)
    require(version(app_version) > version(previous_version), 'application version must increase')
    version(assets_version)
    board_name = LAN_BOARD_NAME if development_lan else BOARD_NAME
    git = lambda *args: subprocess.run(['git', '-C', str(snapshot), *args], capture_output=True, text=True, check=True).stdout.strip()
    sha = git('rev-parse', 'HEAD')
    require(re.fullmatch(r'[0-9a-f]{40}', sha) is not None and not git('status', '--porcelain', '--untracked-files=all'),
            'source snapshot must be a clean committed worktree')
    build = snapshot / 'build'
    cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8')
    require(re.search(r'(?m)^BOARD_NAME:[^=]+=' + re.escape(board_name) + r'$', cache), 'wrong board name')
    require(re.search(r'(?m)^CMAKE_HOME_DIRECTORY:[^=]+=' + re.escape(str(snapshot)) + r'$', cache), 'build is not from source snapshot')
    description = json.loads((build / 'project_description.json').read_text(encoding='utf-8'))
    require(description.get('project_name') == 'xiaozhi' and description.get('project_version') == app_version
            and description.get('target') == 'esp32s3' and description.get('app_bin') == 'xiaozhi.bin'
            and Path(description.get('project_path', '')).resolve() == snapshot
            and Path(description.get('build_dir', '')).resolve() == build.resolve()
            and Path(description.get('config_file', '')).resolve() == (snapshot / 'sdkconfig').resolve(),
            'build description does not match snapshot')
    cfg = config_values(snapshot / 'sdkconfig')
    required = {'CONFIG_IDF_TARGET': '"esp32s3"', 'CONFIG_ESPTOOLPY_FLASHSIZE_16MB': 'y',
                'CONFIG_SPIRAM': 'y', 'CONFIG_SPIRAM_MODE_QUAD': 'y',
                'CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI': 'y',
                'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME': '"partitions/v2/16m.csv"',
                'CONFIG_OLED_SSD1306_128X32': 'y',
                'CONFIG_USE_CUSTOM_WAKE_WORD': 'y', 'CONFIG_SR_MN_CN_MULTINET7_QUANT': 'y',
                'CONFIG_FLASH_DEFAULT_ASSETS': 'y', 'CONFIG_OTA_URL': json.dumps(ota_url),
                'CONFIG_XIAOZHI_REQUIRE_RELEASE_MANIFEST': 'y',
                'CONFIG_XIAOZHI_REQUIRE_TTS_TURN_TAGS': 'y',
                'CONFIG_XIAOZHI_CONTROLLED_RELEASE': 'y',
                'CONFIG_XIAOZHI_ALLOW_AUTOMATIC_FIRMWARE_UPGRADE': 'y',
                'CONFIG_CUSTOM_WAKE_WORD': '"da leng zong guan"',
                'CONFIG_CUSTOM_WAKE_WORD_ALIASES': '"ni hao zong guan;zong guan zong guan"'}
    require(all(cfg.get(k) == v for k, v in required.items()), 'release sdkconfig mismatch')
    require(enabled(cfg, 'CONFIG_XIAOZHI_RELEASE_ALLOW_LAN_HTTP') == development_lan, 'LAN development setting mismatch')
    require(not any(enabled(cfg, k) for k in ('CONFIG_XIAOZHI_USB_SCREEN_MIRROR', 'CONFIG_XIAOZHI_USB_TEXT_CHAT',
                'CONFIG_XIAOZHI_DEVELOPMENT_IDENTITY', 'CONFIG_XIAOZHI_AUDIO_DIAGNOSTICS')), 'development option enabled')
    flash_args = json.loads((build / 'flasher_args.json').read_text(encoding='utf-8'))
    require(flash_args.get('extra_esptool_args', {}).get('chip') == 'esp32s3'
            and flash_args.get('flash_settings', {}).get('flash_size') == '16MB', 'wrong build target or flash size')
    actual = {int(k, 0): v for k, v in flash_args['flash_files'].items()}
    require(actual == {offset: name for name, offset in FILES.items()}, 'unexpected flash components')
    blobs = {}
    for name in FILES:
        path = (build / name).resolve()
        require(build.resolve() in path.parents and path.is_file(), 'component outside build directory')
        blobs[name] = path.read_bytes()
        require(bool(blobs[name]), 'empty build component')
    boot, table, ota, app, assets = (blobs[x] for x in FILES)
    require(16 <= len(boot) <= 0x8000 and boot[0] == 0xe9
            and struct.unpack_from('<H', boot, 12)[0] == 9 and len(ota) <= 0x2000,
            'invalid ESP32-S3 bootloader or OTA metadata')
    require(all(b == 0xff for b in ota), 'OTA initial metadata is not blank')
    check_partition_table(table)
    require(len(app) <= APP_SIZE, 'app exceeds OTA partition')
    metadata = read_app(app)
    require(metadata['version'] == app_version and metadata['project'] == 'xiaozhi', 'app descriptor version mismatch')
    sdk = re.match(r'^v?(\d+\.\d+\.\d+)(?:[-+].*)?$', metadata['sdk'])
    require(sdk is not None and version(sdk.group(1)) >= (6, 0, 1), 'unsupported ESP-IDF version')
    require(board_name.encode() in app and ota_url.encode() in app, 'app does not contain expected board or OTA URL')
    check_assets(assets)
    image = bytearray(b'\xff' * FLASH)
    for name, offset in FILES.items():
        data = blobs[name]
        require(offset + len(data) <= FLASH, 'image exceeds flash')
        image[offset:offset+len(data)] = data
    outputs = {'app.bin': app, 'assets.bin': assets, 'first-flash.bin': bytes(image)}
    manifest = {'schema_version': 1, 'board_type': BOARD_TYPE, 'board_name': board_name,
                'chip': 'esp32s3', 'flash_bytes': FLASH, 'psram': 'quad',
                'app_version': app_version, 'previous_app_version': previous_version,
                'assets_version': assets_version, 'source_sha': sha, 'idf_version': metadata['sdk'],
                'ota_url': ota_url, 'first_flash_nvs': 'blank', 'development_only': development_lan,
                'publication_state': 'draft_requires_server_urls_and_authorization',
                'components': {name: {'offset': FILES[name], **digest(data)} for name, data in blobs.items()},
                'firmware': {'schema_version': 1, 'release_id': f'{board_name}-{app_version}',
                    'version': app_version, 'url': None, 'size': len(app),
                    'sha256': hashlib.sha256(app).hexdigest(), 'board_type': BOARD_TYPE,
                    'board_name': board_name, 'chip': 'esp32s3', 'min_flash_size': FLASH,
                    'update_policy': 'automatic', 'assets': {'version': assets_version, 'url': None,
                        'size': len(assets), 'sha256': hashlib.sha256(assets).hexdigest(), 'required': False}},
                'artifacts': {name: digest(data) for name, data in outputs.items()}}
    output.mkdir(parents=True, exist_ok=False)
    for name, data in outputs.items():
        (output / name).write_bytes(data)
    (output / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return manifest


def verify(output, development_lan=False):
    output = Path(output)
    manifest = json.loads((output / 'manifest.json').read_text(encoding='utf-8'))
    board_name = LAN_BOARD_NAME if development_lan else BOARD_NAME
    require(manifest.get('board_type') == BOARD_TYPE and manifest.get('board_name') == board_name
            and manifest.get('chip') == 'esp32s3' and manifest.get('flash_bytes') == FLASH
            and manifest.get('psram') == 'quad' and manifest.get('first_flash_nvs') == 'blank'
            and manifest.get('publication_state') == 'draft_requires_server_urls_and_authorization', 'invalid release identity')
    require(type(manifest.get('development_only')) is bool and manifest['development_only'] == development_lan,
            'development-only packages require explicit development verification')
    require(version(manifest['app_version']) > version(manifest['previous_app_version']), 'version did not increase')
    version(manifest['assets_version']); check_ota_url(manifest['ota_url'], development_lan)
    require(re.fullmatch(r'[0-9a-f]{40}', manifest['source_sha']) is not None, 'invalid source commit')
    require(set(manifest['artifacts']) == {'app.bin', 'assets.bin', 'first-flash.bin'}, 'invalid artifact list')
    files = {}
    for name, expected in manifest['artifacts'].items():
        files[name] = (output / name).read_bytes()
        require(digest(files[name]) == expected, 'artifact digest mismatch')
    image, app, assets = files['first-flash.bin'], files['app.bin'], files['assets.bin']
    firmware = manifest['firmware']
    require(firmware == {'schema_version': 1, 'release_id': f'{board_name}-{manifest["app_version"]}',
        'version': manifest['app_version'], 'url': None, 'size': len(app),
        'sha256': hashlib.sha256(app).hexdigest(), 'board_type': BOARD_TYPE, 'board_name': board_name,
        'chip': 'esp32s3', 'min_flash_size': FLASH, 'update_policy': 'automatic',
        'assets': {'version': manifest['assets_version'], 'url': None, 'size': len(assets),
                   'sha256': hashlib.sha256(assets).hexdigest(), 'required': False}}, 'firmware publication metadata mismatch')
    require(set(manifest['components']) == set(FILES), 'invalid merged component list')
    covered = bytearray(FLASH)
    for name, offset in FILES.items():
        entry = manifest['components'][name]
        size = entry.get('size')
        require(type(size) is int and size > 0 and entry.get('offset') == offset
                and offset + size <= FLASH, 'invalid merged component range')
        require(not any(covered[offset:offset+size]), 'overlapping merged components')
        covered[offset:offset+size] = b'\x01' * size
        require(digest(image[offset:offset+size]) == {'size': size, 'sha256': entry.get('sha256')}, 'merged component digest mismatch')
    require(all(byte == 0xff for byte, used in zip(image, covered) if not used), 'nonblank data outside permitted components')
    require(len(image) == FLASH and image[0x9000:0xd000] == b'\xff' * 0x4000, 'first-flash NVS is not blank')
    require(image[0] == 0xe9 and struct.unpack_from('<H', image, 12)[0] == 9, 'wrong bootloader chip')
    check_partition_table(image[0x8000:0x9000])
    require(image[0xd000:0xf000] == b'\xff' * 0x2000, 'OTA initial metadata is not blank')
    require(len(app) <= APP_SIZE and image[APP_OFFSET:APP_OFFSET+len(app)] == app
            and image[ASSETS_OFFSET:ASSETS_OFFSET+len(assets)] == assets, 'merged image mismatch')
    metadata = read_app(app)
    require(metadata['version'] == manifest['app_version'] and metadata['sdk'] == manifest['idf_version']
            and board_name.encode() in app and manifest['ota_url'].encode() in app, 'app metadata mismatch')
    check_assets(assets)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    prepare = sub.add_parser('prepare-config')
    for name in ('snapshot', 'output', 'ota-url'):
        prepare.add_argument('--' + name, required=True)
    prepare.add_argument('--development-lan', action='store_true')
    make = sub.add_parser('package')
    for name in ('snapshot', 'output', 'ota-url', 'app-version', 'previous-version', 'assets-version'):
        make.add_argument('--' + name, required=True)
    make.add_argument('--development-lan', action='store_true')
    check = sub.add_parser('verify'); check.add_argument('output'); check.add_argument('--development-lan', action='store_true')
    args = parser.parse_args()
    if args.command == 'prepare-config':
        prepare_config(args.snapshot, args.output, args.ota_url, args.development_lan)
        print('Release configuration fragment prepared; no firmware was built.')
        return
    if args.command == 'package':
        package(args.snapshot, args.output, args.ota_url, args.app_version, args.previous_version, args.assets_version, args.development_lan)
    else:
        verify(args.output, args.development_lan)
    print('Offline validation passed; device and OTA acceptance remain required.')


if __name__ == '__main__':
    main()
