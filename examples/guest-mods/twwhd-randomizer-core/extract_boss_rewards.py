"""Extract six per-seed boss constants from the player's randomized USA RPX.

This reads data operands only; it never patches or distributes game instructions.
The randomized stage assets provide the matching respawn ACTR item parameters.
"""
from pathlib import Path
import argparse
import hashlib
import json
import sys

# Upstream location_data.yaml Boss paths; first four use cloud r7 high-immediate byte.
BOSS_SITES = (
    ("Gohma", 0x020F5ED4, True),
    ("Kalle Demos", 0x020AF914, True),
    ("Gohdan", 0x020EFE18, False),
    ("Helmaroc King", 0x0206C094, True),
    ("Jalhalla", 0x020DADDC, True),
    ("Molgera", 0x021031E4, False),
)

def extract(read):
    rewards = []
    for name, address, cloud in BOSS_SITES:
        operand = read(address, 4)
        if len(operand) != 4:
            raise ValueError(f"short boss operand for {name}")
        # li r7,(item<<8)|FF for cloud producers; li r4,item for direct producers.
        if operand[:2] != (b"\x38\xE0" if cloud else b"\x38\x80"):
            raise ValueError(f"unsupported boss operand for {name}")
        if cloud and operand[3] != 0xFF or not cloud and operand[2] != 0:
            raise ValueError(f"unsupported boss operand layout for {name}")
        rewards.append(operand[2] if cloud else operand[3])
    return rewards

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rpx", type=Path)
    parser.add_argument("output", type=Path, help="private module source directory")
    args = parser.parse_args()
    sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
    from rpx import Rpx
    rpx = Rpx(str(args.rpx))
    def read(address, size):
        for section in rpx.sections:
            if section.addr <= address and address + size <= section.addr + len(section.data):
                return section.data[address-section.addr:address-section.addr+size]
        raise ValueError(f"unmapped boss operand {address:08X}")
    rewards = extract(read)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "seed_boss_data.h").write_text(
        "/* Private player-owned seed boss rewards. Gohma, Kalle, Gohdan, Helmaroc, Jalhalla, Molgera. */\n"
        "static const u8 init_reward_boss[6] = {" + ",".join(map(str, rewards)) + "};\n")
    (args.output / "seed-boss-config.json").write_text(json.dumps({
        "source_sha256": hashlib.sha256(args.rpx.read_bytes()).hexdigest(),
        "boss_rewards": dict(zip((site[0] for site in BOSS_SITES), rewards)),
    }, indent=2) + "\n")

if __name__ == "__main__":
    main()
