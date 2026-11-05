import importlib.util
import json
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('bread_package', Path(__file__).resolve().parents[1] / 'package_bread_ai.py')
pkg = importlib.util.module_from_spec(spec); spec.loader.exec_module(pkg)


def assets(commands=pkg.WAKE_WORDS, models=('mn7_cn', 'fst')):
    headers = bytearray(struct.pack('<I', len(models)))
    data_start = 4 + len(models) * (36 + 40)
    for i, model in enumerate(models):
        headers.extend(model.encode().ljust(32, b'\0') + struct.pack('<I', 1))
        headers.extend(b'data'.ljust(32, b'\0') + struct.pack('<II', data_start+i, 1))
    headers.extend(b'x' * len(models))
    index = json.dumps({'srmodels': 'srmodels.bin', 'multinet_model': {'language': 'cn',
        'commands': [{'action': 'wake', 'command': c, 'text': '大冷总管'} for c in sorted(commands)]}}).encode()
    table, payload = bytearray(), bytearray()
    for name, data in [('srmodels.bin', bytes(headers)), ('index.json', index)]:
        table.extend(name.encode().ljust(32, b'\0') + struct.pack('<IIHH', len(data), len(payload), 0, 0))
        payload.extend(b'ZZ' + data)
    body = table + payload
    return struct.pack('<III', 2, sum(body) & 0xffff, len(body)) + body


def partitions():
    items = [('nvs',1,2,0x9000,0x4000), ('otadata',1,0,0xd000,0x2000),
             ('phy_init',1,1,0xf000,0x1000), ('ota_0',0,0x10,0x20000,0x3f0000),
             ('ota_1',0,0x11,0x410000,0x3f0000), ('assets',1,0x82,0x800000,0x800000)]
    return b''.join(struct.pack('<HBBII16sI', 0x50aa, typ, sub, off, size, name.encode(), 0)
                    for name,typ,sub,off,size in items) + b'\xff' * 32


class BreadPackageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); root = Path(self.temp.name)
        self.root = (root / 'snapshot').resolve(); self.root.mkdir(); self.output = root / 'release'
        subprocess.run(['git','init','-q',str(self.root)],check=True)
        subprocess.run(['git','-C',str(self.root),'config','user.email','test@example.invalid'],check=True)
        subprocess.run(['git','-C',str(self.root),'config','user.name','Test'],check=True)
        (self.root / '.gitignore').write_text('build/\nsdkconfig\n')
        (self.root / 'source').write_text('fixture')
        subprocess.run(['git','-C',str(self.root),'add','.'],check=True)
        subprocess.run(['git','-C',str(self.root),'commit','-qm','fixture'],check=True)
        self.build = self.root / 'build'; self.build.mkdir()
        (self.build / 'bootloader').mkdir(); (self.build / 'partition_table').mkdir()
        (self.build / 'CMakeCache.txt').write_text(f'BOARD_NAME:UNINITIALIZED={pkg.BOARD_NAME}\nCMAKE_HOME_DIRECTORY:INTERNAL={self.root}\n')
        self.url = 'https://ota.example.invalid/device/ota/'
        config = {'CONFIG_IDF_TARGET':'"esp32s3"', 'CONFIG_ESPTOOLPY_FLASHSIZE_16MB':'y',
                  'CONFIG_SPIRAM':'y', 'CONFIG_SPIRAM_MODE_QUAD':'y', 'CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI':'y',
                  'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME':'"partitions/v2/16m.csv"', 'CONFIG_OLED_SSD1306_128X32':'y',
                  'CONFIG_USE_CUSTOM_WAKE_WORD':'y', 'CONFIG_SR_MN_CN_MULTINET7_QUANT':'y',
                  'CONFIG_FLASH_DEFAULT_ASSETS':'y', 'CONFIG_OTA_URL':json.dumps(self.url),
                  'CONFIG_XIAOZHI_REQUIRE_RELEASE_MANIFEST':'y', 'CONFIG_XIAOZHI_REQUIRE_TTS_TURN_TAGS':'y',
                  'CONFIG_XIAOZHI_CONTROLLED_RELEASE':'y', 'CONFIG_XIAOZHI_ALLOW_AUTOMATIC_FIRMWARE_UPGRADE':'y',
                  'CONFIG_CUSTOM_WAKE_WORD':'"da leng zong guan"',
                  'CONFIG_CUSTOM_WAKE_WORD_ALIASES':'"ni hao zong guan;zong guan zong guan"'}
        (self.root / 'sdkconfig').write_text(''.join(k+'='+v+'\n' for k,v in config.items()))
        app = bytearray(288); app[0] = 0xe9; struct.pack_into('<H', app, 12, 9); struct.pack_into('<I', app, 32, 0xabcd5432)
        for offset, text in [(48,'2.5.3'), (80,'xiaozhi'), (144,'v6.0.1')]: app[offset:offset+len(text)] = text.encode()
        app.extend(pkg.BOARD_NAME.encode()+b'\0'+self.url.encode()+b'\0')
        boot = bytearray(24); boot[0] = 0xe9; struct.pack_into('<H', boot, 12, 9)
        for path, data in [('bootloader/bootloader.bin',bytes(boot)), ('partition_table/partition-table.bin',partitions()),
                           ('ota_data_initial.bin',b'\xff'*8192), ('xiaozhi.bin',bytes(app)), ('generated_assets.bin',assets())]:
            (self.build / path).write_bytes(data)
        (self.build / 'project_description.json').write_text(json.dumps({'project_name':'xiaozhi', 'project_version':'2.5.3', 'target':'esp32s3', 'app_bin':'xiaozhi.bin', 'project_path':str(self.root), 'build_dir':str(self.build), 'config_file':str(self.root/'sdkconfig')}))
        (self.build / 'flasher_args.json').write_text(json.dumps({'extra_esptool_args': {'chip':'esp32s3'},
            'flash_settings': {'flash_size':'16MB'}, 'flash_files': {hex(offset): name for name,offset in pkg.FILES.items()}}))

    def tearDown(self): self.temp.cleanup()
    def package(self): return pkg.package(self.root,self.output,self.url,'2.5.3','2.5.2','1.0.0')

    def test_package_and_verify_fixture(self):
        manifest = self.package(); self.assertEqual(pkg.verify(self.output), manifest)
        self.assertEqual(manifest['firmware']['update_policy'], 'automatic')
        self.assertIsNone(manifest['firmware']['url'])
        self.assertEqual((self.output / 'first-flash.bin').read_bytes()[0x9000:0xd000], b'\xff'*0x4000)
        with self.assertRaises(ValueError): self.package()

    def test_invalid_inputs_fail_closed(self):
        for url in ('https://api.tenclass.net/xiaozhi/ota/', 'http://ota.example.invalid/', 'https://user:secret@ota.example.invalid/'):
            with self.assertRaises(ValueError): pkg.check_ota_url(url)
        with self.assertRaises(ValueError): pkg.package(self.root,self.output,self.url,'2.5.2','2.5.2','1.0.0')
        (self.build/'generated_assets.bin').write_bytes(assets(commands={'da leng zong guan'}))
        with self.assertRaisesRegex(ValueError, 'three wake'): self.package()
        (self.build/'generated_assets.bin').write_bytes(assets(models=('mn7_cn',)))
        with self.assertRaisesRegex(ValueError, 'FST'): self.package()
        (self.build/'generated_assets.bin').write_bytes(assets())
        (self.build/'CMakeCache.txt').write_text('BOARD_NAME:UNINITIALIZED=wrong\n')
        with self.assertRaisesRegex(ValueError, 'board'): self.package()
        self.assertFalse(self.output.exists())

    def test_explicit_lan_candidate_is_not_a_formal_release(self):
        for url in ('http://10.4.2.1:8080/ota/', 'http://172.16.0.1/ota/',
                    'http://192.168.3.10:18893/ota/', 'http://127.0.0.1:8080/ota/'):
            pkg.check_ota_url(url, True)
            with self.assertRaises(ValueError): pkg.check_ota_url(url)
        for url in ('http://8.8.8.8/ota/', 'http://100.64.0.1/ota/', 'http://localhost/ota/',
                    'http://device.local/ota/', 'http://192.168.1.2:0/ota/', 'http://[::1]/ota/'):
            with self.assertRaises(ValueError): pkg.check_ota_url(url, True)
        lan = 'http://192.168.3.10:18893/ota/'
        source = Path(__file__).resolve().parents[2]
        fragment = Path(self.temp.name)/'lan.defaults'
        pkg.prepare_config(source, fragment, lan, development_lan=True)
        self.assertIn('CONFIG_XIAOZHI_RELEASE_ALLOW_LAN_HTTP=y', fragment.read_text())
        config = self.root/'sdkconfig'
        config.write_text(config.read_text().replace(self.url, lan) + 'CONFIG_XIAOZHI_RELEASE_ALLOW_LAN_HTTP=y\n')
        app = self.build/'xiaozhi.bin'; app.write_bytes(app.read_bytes().replace(self.url.encode(), lan.encode()))
        with self.assertRaisesRegex(ValueError, 'board'):
            pkg.package(self.root,self.output,lan,'2.5.3','2.5.2','1.0.0',development_lan=True)
        cache = self.build/'CMakeCache.txt'; cache.write_text(cache.read_text().replace(pkg.BOARD_NAME, pkg.LAN_BOARD_NAME))
        app.write_bytes(app.read_bytes().replace(pkg.BOARD_NAME.encode(), pkg.LAN_BOARD_NAME.encode()))
        manifest = pkg.package(self.root,self.output,lan,'2.5.3','2.5.2','1.0.0',development_lan=True)
        self.assertTrue(manifest['development_only'])
        self.assertEqual(manifest['board_name'], pkg.LAN_BOARD_NAME)
        self.assertEqual(manifest['firmware']['board_name'], pkg.LAN_BOARD_NAME)
        with self.assertRaises(ValueError): pkg.verify(self.output)
        self.assertEqual(pkg.verify(self.output, development_lan=True), manifest)

    def test_oversized_app_and_unsafe_config_rejected(self):
        app = self.build/'xiaozhi.bin'; original = app.read_bytes()
        app.write_bytes(original + b'x' * (pkg.APP_SIZE + 1))
        with self.assertRaisesRegex(ValueError, 'exceeds OTA'): self.package()
        app.write_bytes(original)
        config = self.root/'sdkconfig'; config.write_text(config.read_text() + 'CONFIG_XIAOZHI_USB_SCREEN_MIRROR=y\n')
        with self.assertRaisesRegex(ValueError, 'development'): self.package()
        self.assertFalse(self.output.exists())

    def test_prepare_config_uses_explicit_ota_without_modifying_source(self):
        source = Path(__file__).resolve().parents[2]
        path = Path(self.temp.name) / 'release-sdkconfig.defaults'
        pkg.prepare_config(source, path, self.url)
        data = path.read_text()
        self.assertIn('CONFIG_OTA_URL="'+self.url+'"', data)
        self.assertIn('CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI=y', data)
        self.assertIn('CONFIG_XIAOZHI_REQUIRE_TTS_TURN_TAGS=y', data)
        self.assertIn('CONFIG_USE_CUSTOM_WAKE_WORD=y', data)
        self.assertIn('CONFIG_LANGUAGE_ZH_CN=y', data)
        self.assertIn('CONFIG_CUSTOM_WAKE_WORD_ALIASES="ni hao zong guan;zong guan zong guan"', data)
        with self.assertRaises(ValueError): pkg.prepare_config(source, path, self.url)

    def test_variant_is_unique_and_controlled(self):
        source = Path(__file__).resolve().parents[2]
        config = json.loads((source/'main/boards/bread-compact-wifi/config.json').read_text())
        variants = [item for item in config['builds'] if item['name'] == pkg.BOARD_NAME]
        self.assertEqual(len(variants), 1)
        options = dict(option.split('=', 1) for option in variants[0]['sdkconfig_append'])
        for key in ('CONFIG_XIAOZHI_REQUIRE_RELEASE_MANIFEST', 'CONFIG_XIAOZHI_REQUIRE_TTS_TURN_TAGS',
                    'CONFIG_XIAOZHI_CONTROLLED_RELEASE', 'CONFIG_XIAOZHI_ALLOW_AUTOMATIC_FIRMWARE_UPGRADE'):
            self.assertEqual(options[key], 'y')
        for key in ('CONFIG_XIAOZHI_DEVELOPMENT_IDENTITY', 'CONFIG_XIAOZHI_AUDIO_DIAGNOSTICS',
                    'CONFIG_XIAOZHI_USB_SCREEN_MIRROR', 'CONFIG_XIAOZHI_USB_TEXT_CHAT'):
            self.assertEqual(options[key], 'n')
        self.assertEqual(options['CONFIG_OTA_URL'], '""')

    def test_partition_and_digest_tampering_rejected(self):
        with self.assertRaises(ValueError): pkg.check_partition_table(partitions().replace(b'assets', b'badset'))
        self.package()
        (self.output/'app.bin').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'digest'): pkg.verify(self.output)

    def test_dirty_source_and_private_ota_metadata_rejected(self):
        (self.root/'source').write_text('changed')
        with self.assertRaisesRegex(ValueError, 'clean committed'): self.package()
        subprocess.run(['git','-C',str(self.root),'checkout','--','source'],check=True)
        (self.build/'ota_data_initial.bin').write_bytes(b'secret')
        with self.assertRaisesRegex(ValueError, 'not blank'): self.package()
