import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from product_profiles import variant_sdkconfig


class ProductProfileTests(unittest.TestCase):
    def test_hardware_options_are_preserved_and_profile_is_shared(self):
        root = Path(__file__).resolve().parents[2]
        hardware = ["CONFIG_SPIRAM_MODE_QUAD=y"]
        options = variant_sdkconfig({"sdkconfig_append": hardware,
                                     "sdkconfig_profile": "daleng-cn"}, root)
        self.assertEqual(options[:1], hardware)
        self.assertEqual(hardware, ["CONFIG_SPIRAM_MODE_QUAD=y"])
        self.assertIn("CONFIG_LANGUAGE_ZH_CN=y", options)
        self.assertIn("CONFIG_USE_CUSTOM_WAKE_WORD=y", options)

    def test_invalid_missing_and_duplicate_profiles_fail_before_build(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("../private", "/etc/passwd", "missing", "", 42):
                with self.subTest(name=name), self.assertRaises(ValueError):
                    variant_sdkconfig({"sdkconfig_profile": name}, root)
            (root / "profiles").mkdir()
            profile = root / "profiles/test.defaults"
            for data in ("CONFIG_EXAMPLE=y\nCONFIG_EXAMPLE=n\n", "invalid\n"):
                profile.write_text(data)
                with self.assertRaises(ValueError):
                    variant_sdkconfig({"sdkconfig_profile": "test"}, root)
            profile.write_text("CONFIG_EXAMPLE=y\n")
            with self.assertRaises(ValueError):
                variant_sdkconfig({"sdkconfig_profile": "test",
                                   "sdkconfig_append": ["CONFIG_EXAMPLE=n"]}, root)
