"""Resolve shared product settings independently of board hardware options."""
import re
from pathlib import Path


def variant_sdkconfig(build: dict, root: Path) -> list[str]:
    options = build.get("sdkconfig_append", [])
    if not isinstance(options, list) or not all(isinstance(item, str) for item in options):
        raise ValueError(f"build {build.get('name')!r} sdkconfig_append must be a string list")
    profile = build.get("sdkconfig_profile")
    if profile is None:
        return list(options)
    if not isinstance(profile, str) or not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", profile):
        raise ValueError("Invalid product profile name")
    path = Path(root) / "profiles" / f"{profile}.defaults"
    if not path.is_file():
        raise ValueError(f"Missing product profile: {path}")
    result = list(options)
    names = {item.split("=", 1)[0] for item in options}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if not re.fullmatch(r"CONFIG_[A-Z0-9_]+=[^\r\n]*", line):
            raise ValueError(f"Invalid assignment in product profile: {path}")
        name = line.split("=", 1)[0]
        if name in names:
            raise ValueError(f"Duplicate product profile assignment: {name}")
        names.add(name)
        result.append(line)
    return result
