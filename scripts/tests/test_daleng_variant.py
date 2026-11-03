"""Guard the opt-in wake-word variant's hardware and development identity."""
import json
import unittest
from pathlib import Path


class DalengVariantTests(unittest.TestCase):
    def test_custom_wake_word_preserves_devmac_configuration(self):
        root = Path(__file__).resolve().parents[2]
        config = json.loads((root / 'main/boards/bread-compact-wifi/config.json').read_text())
        builds = {item['name']: item['sdkconfig_append'] for item in config['builds']}
        base = builds['bread-compact-wifi-128x32-usb-mirror-devmac']
        variant = builds['bread-compact-wifi-128x32-usb-mirror-devmac-daleng']
        self.assertEqual(config['target'], 'esp32s3')
        self.assertEqual(variant[:len(base)], base)
        options = dict(item.split('=', 1) for item in variant)
        self.assertEqual(len(options), len(variant), 'Duplicate assignments can hide overrides')
        for symbol in ('WAKE_WORD_DISABLED', 'USE_ESP_WAKE_WORD', 'USE_AFE_WAKE_WORD',
                       'SR_MN_CN_NONE'):
            self.assertEqual(options['CONFIG_' + symbol], 'n')
        for symbol in ('USE_CUSTOM_WAKE_WORD', 'SR_MN_CN_MULTINET7_QUANT'):
            self.assertEqual(options['CONFIG_' + symbol], 'y')
        self.assertEqual(options['CONFIG_CUSTOM_WAKE_WORD'], '"da leng zong guan"')
        self.assertEqual(options['CONFIG_CUSTOM_WAKE_WORD_DISPLAY'], '"大冷总管"')
        self.assertEqual(options['CONFIG_CUSTOM_WAKE_WORD_THRESHOLD'], '20')
        self.assertEqual(options['CONFIG_CUSTOM_WAKE_WORD_ALIASES'],
                         '"ni hao zong guan;zong guan zong guan"')


if __name__ == '__main__':
    unittest.main()
