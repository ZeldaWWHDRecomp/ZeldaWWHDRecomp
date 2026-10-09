"""Parser checks use declarations only, with no game input."""
import unittest
from public_sdk_index import DECL, VERIFY, mask_declarations, symbols_header
from public_sdk_layouts import layout, vectors
from public_sdk_bindings import bindings, guest_type
from public_sdk_data import declarations, play_declarations
from public_sdk_semantics import semantic_declarations


class PublicDeclarations(unittest.TestCase):
    def test_scalar_and_pointer_return(self):
        for text, result, name in [
            ('\nstatic f32 approach(f32* p, f32 rate) {', 'f32', 'approach'),
            ('\nvoid *construct(void *self) {', 'void *', 'construct'),
        ]:
            match = DECL.search(text)
            self.assertIsNotNone(match)
            self.assertEqual(match[1].strip(), result)
            self.assertEqual(match[2], name)

    def test_layout_omits_nested_and_unknown_fields(self):
        text = """struct Actor {
          struct Inner { /* 0x0 */ be<u32> wrong; };
          /* 0x4 */ be<f32> speed;
          /* 0x8 */ Unknown compound;
          /* 0xC */ gptr<Other> owner;
        }; WWHD_SIZE(Actor, 0x10);"""
        output = layout(text, 'Actor')
        self.assertIn('f32 speed', output)
        self.assertIn('u32 owner', output)
        self.assertIn('__builtin_offsetof(Actor, speed) == 0x4', output)
        self.assertNotIn('wrong', output)
        self.assertNotIn('compound', output)

    def test_curated_aggregate_views(self):
        text = """struct Actor {
          /* 0x314 */ actor_place current;
          /* 0x328 */ csXyz shape_angle;
          /* 0x330 */ cXyz scale;
          /* 0x33C */ Unknown opaque;
        }; WWHD_SIZE(Actor, 0x3AC);"""
        output = layout(text, 'Actor')
        for kind, field, offset in [('actor_place', 'current', '314'),
                                    ('csXyz', 'shape_angle', '328'), ('cXyz', 'scale', '330')]:
            self.assertIn(kind + ' ' + field, output)
            self.assertIn('__builtin_offsetof(Actor, ' + field + ') == 0x' + offset, output)
        self.assertNotIn('Unknown', output)
        with self.assertRaisesRegex(ValueError, 'aggregate extent'):
            layout('struct A { /* 0x4 */ cXyz pos; }; WWHD_SIZE(A, 0x8);', 'A')

    def test_vector_declarations_fail_closed(self):
        source = ('struct cXyz { be<f32> x, y, z; }; WWHD_SIZE(cXyz, 0xC);'
                  'struct csXyz { be<s16> x, y, z; }; WWHD_SIZE(csXyz, 0x6);')
        output = vectors(source)
        self.assertIn('typedef struct cXyz { f32 x, y, z; }', output)
        self.assertIn('__builtin_offsetof(csXyz, y) == 2', output)
        for changed in [source.replace('x, y, z', 'x, y, w', 1),
                        source.replace('0xC', '0x10'), source.replace('be<f32>', 'be<f64>')]:
            with self.assertRaises(ValueError):
                vectors(changed)

    def test_play_declarations_extract_public_semantics(self):
        sources = {
            'include/d/d_com_inf_game.h': (
                'inline u8* dComIfGp_get() { return gabi::call<u8*>(0x025200D4); }'
                'PLAY_PLAYER = 0x5B2C,'),
            'include/d/actor/d_a_mo2.h': (
                'inline const char* dComIfGp_getStartStageName() { return '
                'gabi::at<const char>(dComIfGp_ea() + 0x5134); }'),
            'include/d/actor/d_a_fm_local.h': (
                'static inline bool dComIfGp_event_runCheck() { return '
                'gabi::load<u8>(dComIfGp_ea() + 0x5292) != 0; }'),
            'd/d_s_play_4.cpp': (
                'st8(dComIfGp_ea() + 0x514C, 0); /* offEnableNextStage */'),
        }
        output = '\n'.join(play_declarations(sources.__getitem__))
        self.assertIn('WWHD_GAME_FUNC(0x025200D4, u8*, wwhd_play_get, (void))', output)
        self.assertIn('WWHD_PLAY_ENABLE_NEXT_STAGE_OFFSET 0x514C', output)
        self.assertNotIn('WWHD_PLAY_LAYER_', output)
        for source in sources:
            changed = dict(sources)
            changed[source] = ''
            with self.assertRaisesRegex(ValueError, 'public play'):
                play_declarations(changed.__getitem__)
        duplicate = dict(sources)
        duplicate['d/d_s_play_4.cpp'] *= 2
        with self.assertRaisesRegex(ValueError, 'public play'):
            play_declarations(duplicate.__getitem__)

    def test_ptmf_layout_and_selected_actor_fields(self):
        source = """struct ProcFunc_l {
            /* 0x0 */ be<s16> d; /* 0x2 */ be<s16> i; /* 0x4 */ be<u32> f;
        }; WWHD_SIZE(ProcFunc_l, 8);"""
        output = layout(source, 'ProcFunc_l', {'d', 'i', 'f'})
        self.assertIn('sizeof(ProcFunc_l) == 8', output)
        self.assertIn('__builtin_offsetof(ProcFunc_l, f) == 0x4', output)
        actor = 'struct A { /* 0x4 */ gptr<Model> mpMorf; /* 0x8 */ u32 other; }; WWHD_SIZE(A, 0xC);'
        output = layout(actor, 'A', {'mpMorf'})
        self.assertIn('u32 mpMorf', output)
        self.assertNotIn('other', output)
        with self.assertRaisesRegex(ValueError, 'selected fields'):
            layout(actor.replace('mpMorf', 'changed'), 'A', {'mpMorf'})

    def test_public_song_contracts_and_chime_offset_fail_closed(self):
        player = ('s32 judge = gabi::call<s32>(0x025E1F34 /* mDoAud_tact_judge */, '
                  '(s32)mProcVar5, (s32)mProcVar3);'
                  'm3624 = gabi::call<u32>(0x025E1EFC /* mDoAud_tact_getBeat */);'
                  'gabi::call(0x025E1E94 /* mDoAud_tact_reset */);')
        chime = ('/* 0x3B4 */ gptr<mDoExt_McaMorf> mpMorf;'
                 'WWHD_OFFSET(Act_c, mpMorf, 0x3B4);')
        def generate(player_source=player, chime_source=chime):
            return semantic_declarations(lambda source: player_source if 'player' in source else chime_source, 'public')
        output = generate()
        self.assertIn('s32, wwhd_tact_judge, (s32 index, s32 direction)', output)
        self.assertIn('void, wwhd_tact_reset, (void)', output)
        self.assertIn('WWHD_OFFSET_daObjGong_Act_c_mpMorf 0x3B4', output)
        for original in ['0x025E1F34', '0x025E1EFC', '0x025E1E94', '(s32)mProcVar3']:
            with self.assertRaisesRegex(ValueError, 'public song'):
                generate(player.replace(original, 'changed'))
        with self.assertRaisesRegex(ValueError, 'public chime'):
            generate(chime_source=chime.replace('0x3B4);', '0x3B8);'))
        with self.assertRaisesRegex(ValueError, 'public chime'):
            generate(chime_source=chime + chime)

    def test_unsupported_signature_is_reported(self):
        index = {'revision': 'public', 'functions': [
            {'name': 'execute', 'address': 0x02000000, 'return': 'BOOL',
             'parameters': 'Actor* self, f32 scale'},
            {'name': 'vector', 'address': 0x02000004, 'return': 'UnknownValue',
             'parameters': ''}]}
        text, skipped = bindings(index)
        self.assertIn('s32, wwhd_execute_02000000, (void* self, f32 scale)', text)
        self.assertEqual([f['name'] for f in skipped], ['vector'])
        self.assertEqual(guest_type('bool*'), 'u8*')

    def test_missing_data_binding_fails_closed(self):
        with self.assertRaisesRegex(ValueError, 'public data declaration changed'):
            declarations(lambda source: '', 'public')

    def test_unnamed_parameters_preserve_abi_types(self):
        index = {'revision': 'public', 'functions': [
            {'name': 'unused_args', 'address': 0x02000000, 'return': 's32',
             'parameters': 'void*, int, const cXyz*, f32 rate, unsigned int'}]}
        text, skipped = bindings(index)
        self.assertFalse(skipped)
        self.assertIn('(void* arg0, int arg1, const void* arg2, f32 rate, unsigned int arg4)', text)

    def test_macro_continuation_does_not_pollute_return_type(self):
        text = '#define ACTIVATE() \\\n    Activation activation\n\nvoid execute(void*) { }'
        match = DECL.search(mask_declarations(text))
        self.assertEqual(match[1].strip(), 'void')
        self.assertEqual(match[2], 'execute')

    def test_verified_register_pair_return_is_not_a_c_struct(self):
        index = {'revision': 'public', 'abi_aliases': {'Pair32': 'wwhd_gpr_pair'},
                 'functions': [{'name': 'pair', 'address': 0x02000000,
                                'return': 'Pair32', 'parameters': 'void*'}]}
        text, skipped = bindings(index)
        self.assertFalse(skipped)
        self.assertIn('wwhd_gpr_pair, wwhd_pair_02000000, (void* arg0)', text)
        self.assertIn('WWHD_RESULT_R3', text)

    def test_member_declaration(self):
        match = DECL.search('\ns32 Actor::execute() {')
        self.assertEqual(match[1].strip(), 's32')
        self.assertEqual(match[2], 'Actor::execute')

    def test_ambiguous_symbols_keep_addresses(self):
        index = {'public_source': 'public', 'revision': 'revision',
                 'functions': [{'name': 'execute', 'address': 0x02000000},
                               {'name': 'execute', 'address': 0x02000004},
                               {'name': '&Actor::draw', 'address': 0x02000008}],
                 'unresolved_declarations': []}
        text = symbols_header(index)
        self.assertIn('WWHD_ADDR_execute_02000000 0x02000000', text)
        self.assertNotIn('#define WWHD_ADDR_execute ', text)
        self.assertIn('#define WWHD_ADDR_Actor__draw ', text)

    def test_member_verification_retains_name(self):
        match = VERIFY.search('VERIFY(0x02000000, &Actor::execute);')
        self.assertEqual(match[2], '&Actor::execute')


if __name__ == '__main__':
    unittest.main()
