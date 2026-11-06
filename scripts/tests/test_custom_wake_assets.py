"""Reject incomplete custom wake assets before emitting a broken package."""
import json
from unittest.mock import patch
import importlib.util
import tempfile
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    'custom_assets', Path(__file__).resolve().parents[1] / 'build_default_assets.py')
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


class CustomWakeAssetsTest(unittest.TestCase):
    def test_missing_fst_dependency_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            model = Path(directory) / 'multinet_model' / 'mn7_cn'
            model.mkdir(parents=True)
            (model / 'data').write_bytes(b'model')
            with self.assertRaises(FileNotFoundError):
                BUILD.get_multinet_model_paths(['mn7_cn', 'fst'], directory)

    def test_invalid_enabled_configuration_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'sdkconfig'
            for threshold in ('garbage', 'nan', '101', '100', '0', '0.5'):
                with self.subTest(threshold=threshold):
                    config.write_text('CONFIG_USE_CUSTOM_WAKE_WORD=y\n'
                                      'CONFIG_CUSTOM_WAKE_WORD="da leng zong guan"\n'
                                      'CONFIG_CUSTOM_WAKE_WORD_DISPLAY="大冷总管"\n'
                                      f'CONFIG_CUSTOM_WAKE_WORD_THRESHOLD={threshold}\n')
                    with self.assertRaises(ValueError):
                        BUILD.read_custom_wake_word_from_sdkconfig(config)

    def test_missing_or_blank_required_fields_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'sdkconfig'
            for field in ('CONFIG_CUSTOM_WAKE_WORD', 'CONFIG_CUSTOM_WAKE_WORD_DISPLAY'):
                for value in (None, '', '   '):
                    with self.subTest(field=field, value=value):
                        lines = {
                            'CONFIG_CUSTOM_WAKE_WORD': 'da leng zong guan',
                            'CONFIG_CUSTOM_WAKE_WORD_DISPLAY': '大冷总管',
                        }
                        lines[field] = value
                        config.write_text('CONFIG_USE_CUSTOM_WAKE_WORD=y\n'
                                          'CONFIG_CUSTOM_WAKE_WORD_THRESHOLD=20\n' +
                                          ''.join(f'{key}="{text}"\n' for key, text in lines.items()
                                                  if text is not None))
                        with self.assertRaises(ValueError):
                            BUILD.read_custom_wake_word_from_sdkconfig(config)

    def test_maximum_threshold_is_valid(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'sdkconfig'
            config.write_text('CONFIG_USE_CUSTOM_WAKE_WORD=y\n'
                              'CONFIG_CUSTOM_WAKE_WORD="da leng zong guan"\n'
                              'CONFIG_CUSTOM_WAKE_WORD_DISPLAY="大冷总管"\n'
                              'CONFIG_CUSTOM_WAKE_WORD_THRESHOLD=99\n')
            self.assertEqual(BUILD.read_custom_wake_word_from_sdkconfig(config)['threshold'], .99)

    def test_generated_index_aliases_and_legacy_config(self):
        for aliases, expected in (
            (None, ['da leng zong guan']),
            (' ni hao zong guan ;zong guan zong guan; da leng zong guan;'
             'ni  hao zong guan;; ',
             ['da leng zong guan', 'ni hao zong guan', 'zong guan zong guan']),
        ):
            with self.subTest(aliases=aliases), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                config = root / 'sdkconfig'
                config.write_text('CONFIG_USE_CUSTOM_WAKE_WORD=y\n'
                                  'CONFIG_SR_MN_CN_MULTINET7_QUANT=y\n'
                                  'CONFIG_CUSTOM_WAKE_WORD="da leng zong guan"\n'
                                  'CONFIG_CUSTOM_WAKE_WORD_DISPLAY="大冷总管"\n'
                                  'CONFIG_CUSTOM_WAKE_WORD_THRESHOLD=20\n' +
                                  (f'CONFIG_CUSTOM_WAKE_WORD_ALIASES="{aliases}"\n'
                                   if aliases is not None else ''))
                for name in ('mn7_cn', 'fst'):
                    model = root / 'multinet_model' / name
                    model.mkdir(parents=True)
                    (model / 'data').write_bytes(b'model')

                def build_index(*args, **kwargs):
                    BUILD.generate_index_json(directory, 'srmodels.bin', None, [],
                                              multinet_model_info=args[6])
                    return True

                with patch('sys.argv', ['build_default_assets', '--sdkconfig', str(config),
                                        '--esp_sr_model_path', directory,
                                        '--output', str(root / 'assets.bin')]), \
                     patch.object(BUILD, 'build_assets_integrated', side_effect=build_index):
                    BUILD.main()
                model_info = json.loads((root / 'index.json').read_text())['multinet_model']
                self.assertEqual(model_info, {
                    'language': 'cn', 'duration': 3000, 'threshold': .2,
                    'commands': [{'command': command, 'text': '大冷总管', 'action': 'wake'}
                                 for command in expected],
                })
                if aliases is None:
                    self.assertEqual(BUILD.read_custom_wake_word_from_sdkconfig(config)['aliases'], [])

    def test_product_profile_combines_stock_wakenet_and_four_multinet_labels(self):
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
        from product_profiles import variant_sdkconfig
        source = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config = root / 'sdkconfig'
            config.write_text('\n'.join(variant_sdkconfig({'sdkconfig_profile': 'daleng-cn'}, source)))
            for name in ('mn7_cn', 'fst'):
                model = root / 'multinet_model' / name
                model.mkdir(parents=True)
                (model / 'data').write_bytes(b'model')

            wake_model = root / 'wakenet_model' / 'wn9_nihaoxiaozhi_tts'
            wake_model.mkdir(parents=True)
            (wake_model / 'data').write_bytes(b'wakenet')

            def build_index(*args, **kwargs):
                self.assertEqual(args[0], [str(wake_model)])
                self.assertEqual({Path(path).name for path in args[1]}, {'mn7_cn', 'fst'})
                BUILD.generate_index_json(directory, 'srmodels.bin', None, [],
                                          multinet_model_info=args[6])
                return True

            with patch('sys.argv', ['build_default_assets', '--sdkconfig', str(config),
                                    '--esp_sr_model_path', directory,
                                    '--output', str(root / 'assets.bin')]), \
                 patch.object(BUILD, 'build_assets_integrated', side_effect=build_index):
                BUILD.main()
            commands = json.loads((root / 'index.json').read_text())['multinet_model']['commands']
            self.assertEqual(commands, [
                {'command': command, 'text': text, 'action': 'wake'}
                for command, text in [('ni hao zong guan', '你好总管'),
                                      ('zong guan zong guan', '总管总管'),
                                      ('ni hao xiao yun', '你好小云'),
                                      ('xiao yun xiao yun', '小云小云')]])

    def test_misaligned_blank_and_duplicate_labeled_aliases_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'sdkconfig'
            base = ('CONFIG_USE_CUSTOM_WAKE_WORD=y\n'
                    'CONFIG_CUSTOM_WAKE_WORD="ni hao zong guan"\n'
                    'CONFIG_CUSTOM_WAKE_WORD_DISPLAY="你好总管"\n'
                    'CONFIG_CUSTOM_WAKE_WORD_THRESHOLD=20\n')
            for aliases, labels in [('one;two', '一'), ('one;two', '一;'),
                                    ('one;one', '一;一'), ('ni hao zong guan', '重复')]:
                with self.subTest(aliases=aliases, labels=labels):
                    config.write_text(base + f'CONFIG_CUSTOM_WAKE_WORD_ALIASES="{aliases}"\n'
                                      f'CONFIG_CUSTOM_WAKE_WORD_ALIAS_DISPLAYS="{labels}"\n')
                    with self.assertRaises(ValueError):
                        BUILD.read_custom_wake_word_from_sdkconfig(config)
