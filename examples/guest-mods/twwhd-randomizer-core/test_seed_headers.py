import unittest

from seed_headers import render_startup, render_rewards
from seed_startup import BYTE_FIELDS
from seed_rewards import REWARDS, RAW_REWARDS


class SeedHeaders(unittest.TestCase):
    def test_mesa_requires_entrance_group(self):
        configuration = self.configuration()
        configuration['mesa_door_unlocked'] = 1
        with self.assertRaisesRegex(ValueError, 'requires shuffled entrances'):
            render_startup(configuration)

    def configuration(self):
        result = dict.fromkeys(BYTE_FIELDS, 0)
        result.update(fix_helmaroc_rng=0, invert_sea_compass_x_axis=0, instant_text_boxes=1, reveal_full_sea_chart=1, entrances_shuffled=0, mesa_door_unlocked=0, quarter_hearts=12, start_room=44,
                      start_spawn=0, last_korl_hint_message_number=0xD73, starting_gear=[56, 59], damage_multiplier=3.25)
        return result

    def test_bounded_data_declarations(self):
        output = render_startup(self.configuration(), [0x3510])
        self.assertIn('init_starting_gear[] = {56,59,255}', output)
        self.assertIn('init_last_korl_hint_message_number = 3443;', output)
        self.assertIn('init_use_different_korl_hyrule_text = 0;', output)
        self.assertIn('init_damage_multiplier = 0x1.a000000000000p+1f', output)
        rewards = {key: [255]*len(sites) for key, sites in {**REWARDS, **RAW_REWARDS}.items()}
        self.assertIn('init_reward_tree = 255;', render_rewards(rewards))
        self.assertIn('init_reward_zunari[] = {255};', render_rewards(rewards))
        rewards['tree'] = [256]
        with self.assertRaises(ValueError): render_rewards(rewards)

    def test_reject_missing_and_injected_data(self):
        for key, bad in (('fix_helmaroc_rng', 2), ('fix_helmaroc_rng', True), ('invert_sea_compass_x_axis', 2), ('invert_sea_compass_x_axis', True), ('instant_text_boxes', 2), ('instant_text_boxes', True), ('start_room', '1; injected()'), ('starting_gear', [255]),
                         ('damage_multiplier', float('nan')), ('last_korl_hint_message_number', 0x80000000), ('last_korl_hint_message_number', True), ('quarter_hearts', -1), ('entrances_shuffled', 2)):
            config = self.configuration(); config[key] = bad
            with self.subTest(key=key), self.assertRaises(ValueError): render_startup(config, [1])
        config = self.configuration(); del config['swordless']
        with self.assertRaises(ValueError): render_startup(config, [1])
        with self.assertRaises(ValueError): render_startup(self.configuration(), [])
        with self.assertRaises(ValueError): render_rewards({})


if __name__ == '__main__':
    unittest.main()
