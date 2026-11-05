import unittest
from PIL import Image, ImageDraw
from reticle_measure import read_trace, projection_metrics, red_components


class ReticleMeasurementTest(unittest.TestCase):
    def test_two_dots_and_hud_exclusion(self):
        image = Image.new('RGB', (200, 100), 'black')
        draw = ImageDraw.Draw(image)
        draw.rectangle((90, 48, 92, 50), fill='red')
        draw.rectangle((100, 48, 102, 50), fill='red')
        draw.rectangle((5, 5, 20, 15), fill='red') # hearts outside aiming ROI
        components = red_components(image)
        self.assertEqual(len(components), 2)
        self.assertEqual([c['centre_px'] for c in components], [[91, 49], [101, 49]])
        self.assertEqual([c['area_px'] for c in components], [9, 9])

    def test_connected_streak_and_diagonal_connectivity(self):
        image = Image.new('RGB', (200, 100), 'black')
        ImageDraw.Draw(image).rectangle((90, 48, 110, 50), fill='red')
        components = red_components(image)
        self.assertEqual(len(components), 1)
        self.assertEqual(components[0]['width_px'], 21)
        self.assertEqual(components[0]['centre_px'], [100, 49])
        image = Image.new('RGB', (200, 100), 'black')
        for k in range(3):
            image.putpixel((90 + k, 48 + k), (255, 0, 0))
        self.assertEqual(len(red_components(image)), 1)

    def test_trace_fractions_jumps_and_stream_isolation(self):
        def line(step, draw_pass, fraction, slot, x):
            return f'[reticle] step {step} pass {draw_pass} t {fraction} packet 1234 slot {slot} world 0 0 1 screen {x} 360 0'
        text = '\n'.join([line(1, 10, .5, -1, 640), line(1, 11, 1, -1, 660),
                          line(2, 12, .5, -1, 640), line(2, 13, 1, -1, 660),
                          line(2, 13, 1, 0, 10), line(3, 20, .5, -1, 1000)])
        result = projection_metrics(read_trace(text))
        sight, target = result
        self.assertEqual(sight['consecutive_draw_jumps_px']['count'], 3)
        self.assertEqual(sight['consecutive_draw_jumps_px']['max'], 20)
        self.assertEqual(sight['within_logic_step_jumps_px']['count'], 2)
        self.assertEqual(sight['logic_boundary_jumps_px']['count'], 1)
        self.assertEqual(sight['steps'][0]['fractions'], [.5, 1])
        self.assertEqual(sight['steps'][0]['x_range_px'], 20)
        self.assertIsNone(sight['samples'][-1]['jump_px']) # missing intervening draws
        self.assertEqual(target['consecutive_draw_jumps_px']['count'], 0)

    def test_nonfinite_projection_is_rejected(self):
        with self.assertRaises(ValueError):
            read_trace('[reticle] step 1 pass 1 t 1 packet 1234 slot -1 world 0 0 1 screen nan 360 0')


if __name__ == '__main__':
    unittest.main()
