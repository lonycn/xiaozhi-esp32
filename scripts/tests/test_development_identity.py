import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class DevelopmentIdentityFormatTests(unittest.TestCase):
    def test_mac_and_uuid_validation_before_applying_network_identity(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        if compiler is None:
            self.skipTest("A host C++ compiler is required")
        source = r'''
#include "development_identity_format.h"
#include <cassert>
int main() {
    using namespace development_identity;
    std::array<uint8_t, 6> mac{};
    assert(ParseMac("02:21:64:63:44:5a", mac) && IsLocalUnicast(mac));
    assert(mac[5] == 0x5a);
    assert(ParseMac("74:4D:BD:A2:C4:2C", mac) && !IsLocalUnicast(mac));
    assert(ParseMac("03:21:64:63:44:5a", mac) && !IsLocalUnicast(mac));
    assert(!ParseMac("02:21:64:63:44", mac));
    assert(!ParseMac("02:21:64:63:44:5a trailing", mac));
    assert(!ParseMac("02-21-64-63-44-5a", mac));
    assert(!ParseMac("02:21:64:63:44:zz", mac));
    assert(ValidUuid("b67553bc-4483-4a0f-84a0-ce7e801e8bf7"));
    assert(!ValidUuid("b67553bc_4483_4a0f_84a0_ce7e801e8bf7"));
    assert(!ValidUuid("b67553bc-4483-4a0f-84a0-ce7e801e8bfz"));
    assert(!ValidUuid(""));
    assert(DecideStoredIdentity(false, "", "") == StoredIdentityDecision::Factory);
    assert(DecideStoredIdentity(true, "02:21:64:63:44:5a", "b67553bc-4483-4a0f-84a0-ce7e801e8bf7") == StoredIdentityDecision::Restore);
    assert(DecideStoredIdentity(true, "74:4D:BD:A2:C4:2C", "b67553bc-4483-4a0f-84a0-ce7e801e8bf7") == StoredIdentityDecision::Invalid);
    assert(DecideStoredIdentity(true, "03:21:64:63:44:5a", "b67553bc-4483-4a0f-84a0-ce7e801e8bf7") == StoredIdentityDecision::Invalid);
    assert(DecideStoredIdentity(true, "02:21:64:63:44:5a", "") == StoredIdentityDecision::Invalid);
    assert(DecideStoredIdentity(true, "", "b67553bc-4483-4a0f-84a0-ce7e801e8bf7") == StoredIdentityDecision::Invalid);
    assert(DecideStoredIdentity(true, "02:21:64:63:44:5a", "00000000-0000-0000-0000-000000000000") == StoredIdentityDecision::Invalid);
}
'''
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "identity-format"
            subprocess.run(
                [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-x", "c++",
                 "-I", str(root / "main"), "-", "-o", str(output)],
                input=source, text=True, capture_output=True, check=True,
            )
            subprocess.run([str(output)], check=True)


if __name__ == "__main__":
    unittest.main()
