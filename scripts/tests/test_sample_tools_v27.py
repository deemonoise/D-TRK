"""Offline Stretch/Tempo wire and CLI lifecycle checks; never opens serial."""
import argparse
import contextlib
import io
import json
import struct
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from scripts.tests import test_sample_extract_protocol as legacy

D = legacy.DESK
TOKEN = legacy.TOKEN


def options(**changes):
    result = dict(index=3, from_frame=11, to_frame=2020, crc=0x89ABCDEF, token=TOKEN,
                  cancel_after=None, ratio=150.0, source_bpm=None, target_bpm=None)
    result.update(changes)
    return SimpleNamespace(**result)


def tempo_reply(token=TOKEN, result=0, bpm10=1200, confidence=900, half=600, twice=2400, flags=1):
    return struct.pack('<IBHHHHB', token, result, bpm10, confidence, half, twice, flags)


def unknown_reply(result=0):
    return tempo_reply(result=result, bpm10=0, confidence=0, half=0, twice=0, flags=0)


class WireTests(unittest.TestCase):
    def test_new_ids_and_v27_golden_payloads(self):
        self.assertEqual(27, D.PROTOCOL)
        self.assertEqual(54, D.MSG['SampleTempo'])
        self.assertEqual(55, D.MSG['SampleTempoCancel'])
        self.assertEqual(bytes.fromhex('7856341203efcdab890b000000e4070000'),
                         D.sample_tempo_payload(TOKEN, 3, 0x89ABCDEF, 11, 2020))
        self.assertEqual(bytes.fromhex('7856341203efcdab890b000000e4070000000100800100'),
                         D.sample_edit_payload(TOKEN, 3, 0x89ABCDEF, 11, 2020, mode=1, ratio_q16=98304))

    def test_edit_rejects_mode_ratio_normalize_combinations(self):
        for change in [dict(mode=2), dict(mode=-1), dict(mode=True), dict(ratio_q16=32767),
                       dict(ratio_q16=131073), dict(ratio_q16=0.5), dict(ratio_q16=32768),
                       dict(mode=1, normalize=True)]:
            with self.subTest(change=change), self.assertRaises(ValueError):
                D.sample_edit_payload(TOKEN, 3, 1, 11, 20, **change)
        for q16 in (32768, 65536, 131072):
            self.assertEqual((0, 1, q16), struct.unpack('<BBI',
                             D.sample_edit_payload(TOKEN, 3, 1, 11, 20, mode=1, ratio_q16=q16)[17:]))

    def test_stretch_ratios_percent_and_bpm_match_q16_rounding(self):
        for percentage, expected in [(50,32768),(100,65536),(150,98304),(200,131072)]:
            self.assertEqual(expected, D._sample_stretch_ratio(options(ratio=percentage)))
        for source, target, expected in [(120,80,98304),(20,40,32768),(480,240,131072),
                                         (123.4,100,80871)]:
            self.assertEqual(expected, D._sample_stretch_ratio(options(ratio=None,source_bpm=source,target_bpm=target)))

    def test_bad_stretch_options_are_rejected(self):
        changes = [dict(ratio=v) for v in (49.9,200.1,float('inf'),float('nan'))]
        changes += [dict(source_bpm=120), dict(target_bpm=120), dict(ratio=None),
                    dict(ratio=None,source_bpm=0,target_bpm=120),
                    dict(ratio=None,source_bpm=120,target_bpm=None),
                    dict(ratio=None,source_bpm=None,target_bpm=120),
                    dict(ratio=None,source_bpm=120,target_bpm=float('nan')),
                    dict(ratio=None,source_bpm=float('inf'),target_bpm=120),
                    dict(ratio=None,source_bpm=19.9,target_bpm=20),
                    dict(ratio=None,source_bpm=480.1,target_bpm=240),
                    dict(ratio=None,source_bpm=120,target_bpm=20)]
        for change in changes:
            with self.subTest(change=change), self.assertRaises(ValueError):
                D._sample_stretch_ratio(options(**change))

    def test_output_frame_rounding_bounds_and_input_validation(self):
        for frames, ratio, expected in [(1,32768,1),(3,32768,2),(2009,98304,3014),(4,131072,8)]:
            self.assertEqual(expected, D.sample_stretch_frames(frames,ratio))
        for frames,ratio in [(0,65536),(-1,65536),(1.5,65536),(True,65536),
                             (1,32767),(1,131073),(0xFFFFFFFF,131072)]:
            with self.subTest(frames=frames,ratio=ratio), self.assertRaises(ValueError):
                D.sample_stretch_frames(frames,ratio)

    def test_known_unknown_and_cancelled_tempo_replies(self):
        result = D.parse_sample_tempo(tempo_reply(), TOKEN)
        self.assertEqual(dict(token=TOKEN,result='Ok',bpm10=1200,confidence=900,
                              halfBpm10=600,doubleBpm10=2400,ambiguous=True),result)
        self.assertEqual(1000,D.parse_sample_tempo(tempo_reply(confidence=1000),TOKEN)['confidence'])
        self.assertEqual('Ok',D.parse_sample_tempo(unknown_reply(),TOKEN)['result'])
        self.assertEqual(0,D.parse_sample_tempo(unknown_reply(),TOKEN)['bpm10'])
        self.assertEqual('Cancelled',D.parse_sample_tempo(unknown_reply(14),TOKEN)['result'])
        for bpm,half,twice in [(400,200,800),(2400,1200,4800)]:
            self.assertEqual(bpm,D.parse_sample_tempo(tempo_reply(bpm10=bpm,half=half,twice=twice),TOKEN)['bpm10'])

    def test_tempo_rejects_malformed_terminal_metadata(self):
        bad = [tempo_reply()[:n] for n in range(14)] + [tempo_reply()+b'\0']
        changes = [dict(token=TOKEN+1),dict(result=13),dict(result=15),dict(result=255),
                   dict(confidence=1001),dict(flags=2),dict(bpm10=399),dict(bpm10=2401),
                   dict(half=199),dict(half=1201),dict(twice=799),dict(twice=4801),dict(result=7)]
        bad += [tempo_reply(**change) for change in changes]
        for change in [dict(confidence=1),dict(half=600),dict(twice=2400),dict(flags=1)]:
            values = dict(bpm10=0,confidence=0,half=0,twice=0,flags=0); values.update(change)
            bad.append(tempo_reply(**values))
        for payload in bad:
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                D.parse_sample_tempo(payload,TOKEN)

    def test_exact_wav_limit_and_selected_range_arithmetic(self):
        limit = (0xFFFFFFFF - 512) // 2
        self.assertEqual(limit,D.sample_stretch_frames(limit,65536))
        with self.assertRaises(ValueError): D.sample_stretch_frames(limit+1,65536)
        payload,_ = D._sample_stretch_options(options(from_frame=1,to_frame=limit//2+1,ratio=200))
        self.assertEqual((1,limit//2+1),struct.unpack_from('<II',payload,9))

    def test_ids_selftest_refuses_each_bad_message_id(self):
        for name in ('Debug','WtRender','RenderOut','WtFrame16','WtAudition',
                     'SampleEdit','SampleEditCancel','SampleTempo','SampleTempoCancel'):
            with self.subTest(name=name),patch.dict(D.MSG,{name:D.MSG[name]+1}),self.assertRaises(AssertionError):
                D._selftest_ids()

    def test_zero_operation_token_is_never_a_valid_terminal(self):
        for parse,payload,extra in [(D.parse_sample_tempo,tempo_reply(token=0),()),
                                    (D.parse_sample_edit,legacy.reply(token=0),(2009,))]:
            with self.subTest(parse=parse), self.assertRaises(ValueError):
                parse(payload,0,*extra)


class CommandTests(unittest.TestCase):
    def invoke(self, command, link, args=None, code=None):
        output=io.StringIO()
        with contextlib.redirect_stdout(output),patch.object(D.time,'monotonic',side_effect=lambda:link.now):
            if code is None:
                command(link,args or options())
            else:
                with self.assertRaises(SystemExit) as result: command(link,args or options())
                self.assertEqual(code,result.exception.code)
        records=output.getvalue().splitlines()
        self.assertEqual(1,len(records),output.getvalue())
        prefix,body=records[0].split(' ',1)
        return prefix,json.loads(body)

    def test_stretch_success_expects_rounded_length_and_does_not_bind(self):
        link=legacy.FakeLink(response=legacy.reply(frames=3014))
        prefix,result=self.invoke(D.cmd_sample_stretch,link)
        self.assertEqual('SAMPLE_STRETCH_RESULT',prefix)
        self.assertEqual('ok',result['status']);self.assertTrue(result['terminal_ack'])
        self.assertEqual(3014,result['frames']);self.assertEqual([],link.sent)
        self.assertEqual([('SampleEdit',D.sample_edit_payload(TOKEN,3,0x89ABCDEF,11,2020,mode=1,ratio_q16=98304),5.0)],link.calls)

    def test_wrong_stretch_length_is_not_success(self):
        link=legacy.FakeLink()
        _,result=self.invoke(D.cmd_sample_stretch,link,code=1)
        self.assertEqual('invalid_reply',result['status']);self.assertFalse(result['terminal_ack'])
        self.assertEqual([('SampleEditCancel',struct.pack('<I',TOKEN))],link.sent)

    def test_tempo_success_and_unknown_are_read_only(self):
        for payload,bpm in [(tempo_reply(),1200),(unknown_reply(),0)]:
            link=legacy.FakeLink(response=payload)
            prefix,result=self.invoke(D.cmd_sample_tempo,link)
            self.assertEqual('SAMPLE_TEMPO_RESULT',prefix);self.assertEqual('ok',result['status'])
            self.assertEqual(bpm,result['bpm10']);self.assertTrue(result['terminal_ack'])
            self.assertNotIn('crc',result);self.assertNotIn('frames',result)
            self.assertEqual([('SampleTempo',D.sample_tempo_payload(TOKEN,3,0x89ABCDEF,11,2020),5.0)],link.calls)
            self.assertEqual([],link.sent)

    def test_tempo_cancel_and_ctrl_c_wait_for_typed_terminal(self):
        link=legacy.FakeLink(interrupt=True,waited=unknown_reply(14))
        _,result=self.invoke(D.cmd_sample_tempo,link,code=130)
        self.assertEqual('cancelled',result['status']);self.assertTrue(result['terminal_ack'])
        self.assertEqual([('SampleTempoCancel',struct.pack('<I',TOKEN))],link.sent)
        self.assertEqual([('SampleTempo',5.0)],link.waits)
        link=legacy.FakeLink(response=unknown_reply(14),progresses=[(1,1,10)])
        _,result=self.invoke(D.cmd_sample_tempo,link,options(cancel_after=1),code=1)
        self.assertTrue(result['cancel_sent']);self.assertTrue(result['terminal_ack'])

    def test_tempo_no_reply_bad_token_and_crc_are_never_hidden(self):
        for payload,status in [(b'','no_reply'),(tempo_reply(token=2),'invalid_reply')]:
            link=legacy.FakeLink(response=payload)
            _,result=self.invoke(D.cmd_sample_tempo,link,code=1)
            self.assertEqual(status,result['status']);self.assertFalse(result['terminal_ack'])
            self.assertIsNone(result['bpm10']);self.assertEqual(0,result['link_errors'])
            self.assertEqual([('SampleTempoCancel',struct.pack('<I',TOKEN))],link.sent)
        for command,payload in [(D.cmd_sample_tempo,tempo_reply()),(D.cmd_sample_stretch,legacy.reply(frames=3014))]:
            link=legacy.FakeLink(response=payload);link.errors=1
            _,result=self.invoke(command,link,code=1)
            self.assertEqual('link_error',result['status']);self.assertEqual('Ok',result['result'])
            self.assertTrue(result['terminal_ack']);self.assertEqual(1,result['link_errors'])

    def test_invalid_tempo_handshake_never_starts_or_cancels(self):
        link=legacy.FakeLink();link.hello=lambda:(26,'old',0,30624,0)
        _,result=self.invoke(D.cmd_sample_tempo,link,code=1)
        self.assertEqual('protocol_error',result['status']);self.assertEqual([],link.calls)
        self.assertEqual([],link.sent);self.assertIsNone(result['confidence'])

    def test_main_rejects_bad_stretch_before_opening_serial(self):
        tails=[['--ratio','49'],['--ratio','nan'],['--source-bpm','120'],
               ['--source-bpm','120','--target-bpm','20'],['--ratio','150','--target-bpm','100']]
        for tail in tails:
            argv=['link_desk.py','sample-stretch','3','11','2020','--crc','89abcdef']+tail
            with self.subTest(tail=tail),patch.object(D.sys,'argv',argv),patch.object(D,'Link') as opened,contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as result:D.main()
                self.assertEqual(2,result.exception.code);opened.assert_not_called()

    def test_main_dispatches_new_commands_and_parses_bpm_pair(self):
        for command,payload,tail,kind in [('sample-tempo',tempo_reply(),[],'SampleTempo'),
              ('sample-stretch',legacy.reply(frames=3014),['--source-bpm','120','--target-bpm','80'],'SampleEdit')]:
            link=legacy.FakeLink(response=payload)
            argv=['link_desk.py','--port','FAKE',command,'3','11','2020','--crc','89abcdef','--token',str(TOKEN)]+tail
            with self.subTest(command=command),patch.object(D.sys,'argv',argv),patch.object(D,'Link',return_value=link),contextlib.redirect_stdout(io.StringIO()):D.main()
            self.assertEqual(kind,link.calls[0][0]);self.assertFalse(link.alive)

    def test_parser_requires_explicit_stretch_duration_choice(self):
        parser=argparse.ArgumentParser()
        D.add_sample_analysis_parsers(parser.add_subparsers(dest='cmd'))
        with contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):
            parser.parse_args(['sample-stretch','0','0','1','--crc','1'])

    def test_main_no_command_and_selftest_never_open_link(self):
        with patch.object(D.sys,'argv',['link_desk.py']),patch.object(D,'Link') as opened,contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as result:D.main()
            self.assertEqual(2,result.exception.code);opened.assert_not_called()
        with patch.object(D.sys,'argv',['link_desk.py','selftest']),patch.object(D,'Link') as opened,contextlib.redirect_stdout(io.StringIO()):
            D.main();opened.assert_not_called()

    def test_main_keeps_existing_parser_defaults_and_port_choice(self):
        cases=[(['perf'],'cmd_perf',dict(fx=1,depth=0)),
               (['hint','0'],'cmd_hint',dict(ofs=0)),
               (['bench-stream'],'cmd_bench_stream',dict(semis=0))]
        for tail,name,values in cases:
            link=legacy.FakeLink()
            with self.subTest(tail=tail),patch.object(D.sys,'argv',['link_desk.py','--port','FAKE']+tail), \
                 patch.object(D,'Link',return_value=link) as opened,patch.object(D,'find_port') as auto, \
                 patch.object(D,name) as command:
                D.main();opened.assert_called_once_with('FAKE');auto.assert_not_called()
                for field,value in values.items():self.assertEqual(value,getattr(command.call_args.args[1],field))
        with patch.object(D.sys,'argv',['link_desk.py','perf']),patch.object(D,'Link',return_value=legacy.FakeLink()) as opened, \
             patch.object(D,'find_port',return_value='AUTO') as auto,patch.object(D,'cmd_perf'):
            D.main();opened.assert_called_once_with('AUTO');auto.assert_called_once_with()

    def test_stretch_rejects_canonical_wav_overflow(self):
        with self.assertRaises(ValueError):
            D._sample_stretch_options(options(from_frame=0,to_frame=0xFFFFFFFF,ratio=200))


if __name__=='__main__':unittest.main()
