"""SampleEdit protocol and command lifecycle; no serial module or device is used."""
import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).absolute().parents[2]
SPEC = importlib.util.spec_from_file_location('sample_extract_desk', ROOT / 'scripts/link_desk.py')
DESK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DESK)
TOKEN = 0x12345678


def args(**changes):
    values = dict(index=3, from_frame=11, to_frame=2020, crc=0x89ABCDEF,
                  normalize=False, token=TOKEN, cancel_after=None)
    values.update(changes)
    return SimpleNamespace(**values)


def reply(result=0, token=TOKEN, frames=2009, rate=22050, root=48):
    return struct.pack('<IBIIIB', token, result, 0xAABBCCDD, frames, rate, root)


class FakeLink:
    def __init__(self, response=None, progresses=(), interrupt=False, waited=None, send_error=False):
        self.response = reply() if response is None else response
        self.progresses, self.interrupt, self.waited = progresses, interrupt, waited
        self.send_error = send_error
        self.calls, self.sent, self.waits, self.sent_times = [], [], [], []
        self.now = 0.0
        self.errors = 0

    def hello(self):
        return (DESK.PROTOCOL, 'fake', 1, 30624, 0)

    def call(self, name, payload, timeout, progress):
        self.calls.append((name, payload, timeout))
        for when, done, total in self.progresses:
            self.now = when
            progress(done, total)
        if self.interrupt:
            raise KeyboardInterrupt
        return self.response

    def send(self, name, payload):
        self.sent.append((name, payload))
        self.sent_times.append(self.now)
        if self.send_error:
            raise OSError('closed')

    def wait(self, name, timeout):
        self.waits.append((name, timeout))
        return self.waited


class SampleExtractProtocolTests(unittest.TestCase):
    def test_protocol_and_message_ids_append_without_renumbering(self):
        self.assertEqual(28, DESK.PROTOCOL)
        self.assertEqual(51, DESK.MSG['WtAudition'])
        self.assertEqual(52, DESK.MSG['SampleEdit'])
        self.assertEqual(53, DESK.MSG['SampleEditCancel'])
        self.assertEqual('Cancelled', DESK.BANK_RESULT[14])

    def test_request_23_byte_golden(self):
        expected = bytes.fromhex('7856341203efcdab890b000000e4070000010000000100')
        self.assertEqual(expected, DESK.sample_edit_payload(TOKEN, 3, 0x89ABCDEF, 11, 2020, True))
        self.assertEqual(23, len(expected))

    def test_request_field_bounds_and_invalid_arguments(self):
        fields = [TOKEN, 3, 0x89ABCDEF, 11, 2020, False]
        for position, invalid in [(0, 0), (0, -1), (0, 1 << 32), (1, -1), (1, 128),
                                  (2, -1), (2, 1 << 32), (3, -1), (3, 1 << 32),
                                  (4, 1 << 32), (5, 2), (1, 2.5)]:
            case = fields.copy(); case[position] = invalid
            with self.subTest(case=case), self.assertRaises(ValueError):
                DESK.sample_edit_payload(*case)
        for start, end in [(5, 5), (6, 5), (0, 0)]:
            with self.assertRaises(ValueError):
                DESK.sample_edit_payload(TOKEN, 0, 0, start, end, False)
        self.assertEqual(23, len(DESK.sample_edit_payload(0xFFFFFFFF, 127, 0xFFFFFFFF,
                                                         0xFFFFFFFE, 0xFFFFFFFF, False)))

    def test_reply_18_byte_success(self):
        result = DESK.parse_sample_edit(reply(), TOKEN, 2009)
        self.assertEqual(dict(token=TOKEN, result='Ok', crc=0xAABBCCDD,
                              frames=2009, rate=22050, root=48), result)

    def test_reply_rejects_truncated_extra_unknown_running_and_wrong_token(self):
        cases = [reply()[:n] for n in range(18)] + [reply()+b'\x00', reply(result=15),
                 reply(result=255), reply(result=13), reply(token=TOKEN+1)]
        for payload in cases:
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                DESK.parse_sample_edit(payload, TOKEN, 2009)

    def test_reply_validates_success_metadata_and_exact_frames(self):
        for change in [dict(frames=0), dict(frames=2008), dict(rate=0), dict(rate=48000), dict(root=128)]:
            with self.subTest(change=change), self.assertRaises(ValueError):
                DESK.parse_sample_edit(reply(**change), TOKEN, 2009)
        cancelled = DESK.parse_sample_edit(reply(result=14, frames=0, rate=0), TOKEN, 2009)
        self.assertEqual('Cancelled', cancelled['result'])


class SampleExtractCommandTests(unittest.TestCase):
    def command(self, link, options=None, expected_exit=None):
        output = io.StringIO()
        with contextlib.redirect_stdout(output), patch.object(DESK.time, 'monotonic', side_effect=lambda: link.now):
            if expected_exit is None:
                DESK.cmd_sample_extract(link, options or args())
            else:
                with self.assertRaises(SystemExit) as exit_info:
                    DESK.cmd_sample_extract(link, options or args())
                self.assertEqual(expected_exit, exit_info.exception.code)
        reports = [line.split(' ', 1)[1] for line in output.getvalue().splitlines()
                   if line.startswith('SAMPLE_EDIT_RESULT ')]
        self.assertEqual(1, len(reports), output.getvalue())
        return json.loads(reports[0])

    def test_success_calls_long_operation_with_exact_request(self):
        link = FakeLink()
        report = self.command(link)
        self.assertEqual('ok', report['status'])
        self.assertTrue(report['terminal_ack'])
        self.assertFalse(report['cancel_sent'])
        self.assertEqual(TOKEN, report['token'])
        self.assertEqual(2009, report['frames'])
        self.assertEqual([('SampleEdit', DESK.sample_edit_payload(TOKEN, 3, 0x89ABCDEF, 11, 2020, False), 5.0)], link.calls)
        self.assertEqual([], link.sent)

    def test_cancel_after_progress_sends_token_once_and_waits_for_terminal_reply(self):
        link = FakeLink(reply(result=14, frames=0, rate=0), progresses=[(.1, 1, 10), (1.1, 2, 10), (2.2, 3, 10)])
        report = self.command(link, args(cancel_after=1), expected_exit=1)
        self.assertEqual('cancelled', report['status'])
        self.assertTrue(report['terminal_ack'])
        self.assertTrue(report['cancel_sent'])
        self.assertEqual([('SampleEditCancel', struct.pack('<I', TOKEN))], link.sent)

    def test_no_reply_cancels_without_claiming_acknowledgement(self):
        link = FakeLink(response=b'')
        report = self.command(link, expected_exit=1)
        self.assertEqual('no_reply', report['status'])
        self.assertFalse(report['terminal_ack'])
        self.assertTrue(report['cancel_sent'])
        self.assertEqual([('SampleEditCancel', struct.pack('<I', TOKEN))], link.sent)

    def test_invalid_terminal_reply_is_failure_and_never_success(self):
        for payload in [reply(token=2), reply(frames=2008), b'bad']:
            with self.subTest(payload=payload):
                report = self.command(FakeLink(payload), expected_exit=1)
                self.assertEqual('invalid_reply', report['status'])
                self.assertFalse(report['terminal_ack'])

    def test_device_error_is_reported_before_failure_exit(self):
        report = self.command(FakeLink(reply(result=7, frames=0, rate=0)), expected_exit=1)
        self.assertEqual('error', report['status'])
        self.assertEqual('Unsupported', report['error'])
        self.assertTrue(report['terminal_ack'])

    def test_ctrl_c_sends_cancel_and_waits_for_actual_terminal_result(self):
        link = FakeLink(interrupt=True, waited=reply(result=14, frames=0, rate=0))
        report = self.command(link, expected_exit=130)
        self.assertEqual('cancelled', report['status'])
        self.assertTrue(report['terminal_ack'])
        self.assertEqual([('SampleEditCancel', struct.pack('<I', TOKEN))], link.sent)
        self.assertEqual([('SampleEdit', 5.0)], link.waits)

    def test_ctrl_c_without_terminal_reply_reports_no_ack(self):
        link = FakeLink(interrupt=True)
        report = self.command(link, expected_exit=130)
        self.assertEqual('interrupted', report['status'])
        self.assertFalse(report['terminal_ack'])
        self.assertTrue(report['cancel_sent'])

    def test_failed_best_effort_cancel_is_honest(self):
        report = self.command(FakeLink(response=b'', send_error=True), expected_exit=1)
        self.assertFalse(report['cancel_sent'])
        self.assertEqual('closed', report['cancel_error'])
        self.assertFalse(report['terminal_ack'])

    def test_protocol_mismatch_never_sends_edit(self):
        link = FakeLink()
        link.hello = lambda: (26, 'old', 1, 30624, 0)
        report = self.command(link, expected_exit=1)
        self.assertEqual('protocol_error', report['status'])
        self.assertEqual([], link.calls)

    def test_auto_token_is_nonzero_uint32_and_invalid_cancel_delay_is_rejected(self):
        for delay in [-1, float('nan'), float('inf')]:
            with self.subTest(delay=delay), self.assertRaises(ValueError):
                DESK.cmd_sample_extract(FakeLink(), args(cancel_after=delay))
        link = FakeLink()
        with patch.object(DESK, 'now_us', return_value=0):
            link.response = reply(token=1)
            report = self.command(link, args(token=None))
        self.assertEqual(1, report['token'])


class SampleExtractFailureAndParserTests(unittest.TestCase):
    command = SampleExtractCommandTests.command
    def test_handshake_failures_never_start_or_cancel_an_edit(self):
        for failure in [None, OSError('hello port closed')]:
            link = FakeLink()
            if failure is None:
                link.hello = lambda: None
            else:
                def broken():
                    raise failure
                link.hello = broken
            report = self.command(link, expected_exit=1)
            self.assertEqual('protocol_error', report['status'])
            self.assertEqual([], link.calls)
            self.assertEqual([], link.sent)
            self.assertFalse(report['terminal_ack'])

    def test_ctrl_c_before_start_sends_no_cancel(self):
        link = FakeLink()
        def interrupted():
            raise KeyboardInterrupt
        link.hello = interrupted
        report = self.command(link, expected_exit=130)
        self.assertEqual('interrupted', report['status'])
        self.assertEqual([], link.sent)
        self.assertEqual([], link.calls)

    def test_failed_call_cancels_without_an_acknowledgement(self):
        link = FakeLink()
        def broken(*_args, **_kwargs):
            raise OSError('read failed')
        link.call = broken
        report = self.command(link, expected_exit=1)
        self.assertEqual('no_reply', report['status'])
        self.assertEqual('read failed', report['error'])
        self.assertTrue(report['cancel_sent'])
        self.assertFalse(report['terminal_ack'])

    def test_failed_or_interrupted_cancel_wait_is_honest(self):
        for failure in [OSError('wait failed'), KeyboardInterrupt()]:
            link = FakeLink(interrupt=True)
            def broken(*_args):
                raise failure
            link.wait = broken
            report = self.command(link, expected_exit=130)
            self.assertEqual('interrupted', report['status'])
            self.assertFalse(report['terminal_ack'])
            self.assertTrue(report['error'])
            self.assertEqual(1, len(link.sent))

    def test_progress_cancel_and_ctrl_c_still_send_only_once(self):
        link = FakeLink(progresses=[(1.0, 1, 10)], interrupt=True,
                        waited=reply(result=14, frames=0, rate=0))
        report = self.command(link, args(cancel_after=0), expected_exit=130)
        self.assertEqual('cancelled', report['status'])
        self.assertEqual(1, len(link.sent))

    def test_cancel_racing_completion_reports_actual_success(self):
        link = FakeLink(progresses=[(.1, 1, 1)])
        report = self.command(link, args(cancel_after=0))
        self.assertEqual('ok', report['status'])
        self.assertTrue(report['terminal_ack'])
        self.assertTrue(report['cancel_sent'])

    def test_parser_supports_decimal_ranges_hex_crc_and_optional_flags(self):
        parser = argparse.ArgumentParser()
        DESK.add_sample_extract_parser(parser.add_subparsers(dest='cmd'))
        parsed = parser.parse_args(['sample-extract', '127', '0x10', '2000', '--crc', '89abcdef',
                                    '--normalize', '--token', '0xffffffff', '--cancel-after', '1.5'])
        payload, token = DESK._sample_extract_options(parsed)
        self.assertEqual(0xFFFFFFFF, token)
        self.assertEqual(DESK.sample_edit_payload(token, 127, 0x89ABCDEF, 16, 2000, True), payload)
        self.assertEqual(1.5, parsed.cancel_after)

    def test_main_rejects_bad_arguments_before_opening_serial(self):
        cases = [['-1', '0', '1', '--crc', '123'], ['0', '10', '9', '--crc', '123'],
                 ['0', '0', '1', '--crc', '100000000'], ['0', '0', '1', '--crc', '1', '--token', '0'],
                 ['0', '0', '1', '--crc', '1', '--cancel-after', '-1'], ['0', '0', '1']]
        for tail in cases:
            with self.subTest(tail=tail), patch.object(DESK.sys, 'argv', ['link_desk.py', 'sample-extract']+tail), \
                    patch.object(DESK, 'Link') as opened, contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as exited:
                    DESK.main()
                self.assertEqual(2, exited.exception.code)
                opened.assert_not_called()

    def test_main_dispatches_sample_extract_and_updates_selftest(self):
        link = FakeLink()
        with patch.object(DESK.sys, 'argv', ['link_desk.py', '--port', 'FAKE', 'sample-extract', '3', '11', '2020',
                                           '--crc', '89abcdef', '--token', str(TOKEN)]), \
                patch.object(DESK, 'Link', return_value=link), contextlib.redirect_stdout(io.StringIO()):
            DESK.main()
        self.assertFalse(link.alive)
        self.assertEqual('SampleEdit', link.calls[0][0])
        with contextlib.redirect_stdout(io.StringIO()):
            DESK.cmd_selftest(SimpleNamespace())


class SampleExtractBoundaryTests(unittest.TestCase):
    command = SampleExtractCommandTests.command

    def test_default_normalize_is_exact_copy_and_reply_accepts_limit_values(self):
        self.assertEqual(0, DESK.sample_edit_payload(TOKEN, 3, 0, 0, 1)[17])
        for rate in (1, 44100):
            result = DESK.parse_sample_edit(reply(rate=rate, root=127), TOKEN, 2009)
            self.assertEqual(rate, result['rate'])
            self.assertEqual(127, result['root'])
        with self.assertRaises(ValueError):
            DESK.parse_sample_edit(reply(frames=0), TOKEN, 0)

    def test_cancel_after_uses_elapsed_time_and_fires_at_exact_threshold(self):
        link = FakeLink(reply(result=14, frames=0, rate=0),
                        progresses=[(10.25, 1, 10), (11.0, 2, 10), (12.0, 3, 10)])
        link.now = 10.0
        report = self.command(link, args(cancel_after=1), expected_exit=1)
        self.assertEqual([11.0], link.sent_times)
        self.assertEqual('cancelled', report['status'])
        plain = FakeLink(progresses=[(10.0, 1, 10)])
        self.command(plain)
        self.assertEqual([], plain.sent)

    def test_cancel_wait_error_preserves_exception_details(self):
        for failure, message in [(OSError('wait failed'), 'wait failed'),
                                 (KeyboardInterrupt(), 'KeyboardInterrupt')]:
            link = FakeLink(interrupt=True)
            def broken(*_args):
                raise failure
            link.wait = broken
            report = self.command(link, expected_exit=130)
            self.assertEqual(message, report['error'])

    def test_result_is_flushed_before_failure_exit_with_unknown_metadata(self):
        link = FakeLink(response=b'')
        with patch('builtins.print') as emitted, self.assertRaises(SystemExit):
            DESK.cmd_sample_extract(link, args())
        emitted.assert_called_once()
        self.assertEqual({'flush': True}, emitted.call_args.kwargs)
        prefix, payload = emitted.call_args.args[0].split(' ', 1)
        self.assertEqual('SAMPLE_EDIT_RESULT', prefix)
        report = json.loads(payload)
        for field in ('crc', 'frames', 'rate', 'root', 'result'):
            self.assertIsNone(report[field])
        self.assertFalse(report['terminal_ack'])

    def test_parser_requires_crc_before_request_validation(self):
        parser = argparse.ArgumentParser()
        DESK.add_sample_extract_parser(parser.add_subparsers(dest='cmd'))
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            parser.parse_args(['sample-extract', '0', '0', '1'])


class SampleExtractLinkHealthTests(unittest.TestCase):
    command = SampleExtractCommandTests.command

    def test_valid_success_with_receive_errors_reports_operation_and_link_failure(self):
        link = FakeLink()
        link.errors = 1
        report = self.command(link, expected_exit=1)
        self.assertEqual('link_error', report['status'])
        self.assertEqual('Ok', report['result'])
        self.assertTrue(report['terminal_ack'])
        self.assertEqual(2009, report['frames'])
        self.assertEqual(1, report['link_errors'])
        self.assertEqual(1, link.errors)  # startup CRC is not reset or ignored

    def test_every_report_includes_cumulative_link_errors_without_overriding_errors(self):
        cases = [(reply(), None, 'ok', 0),
                 (reply(result=14, frames=0, rate=0), 1, 'cancelled', 3),
                 (reply(result=7, frames=0, rate=0), 1, 'error', 2),
                 (b'', 1, 'no_reply', 4)]
        for payload, code, status, errors in cases:
            with self.subTest(status=status):
                link = FakeLink(payload)
                link.errors = errors
                report = self.command(link, expected_exit=code)
                self.assertEqual(status, report['status'])
                self.assertEqual(errors, report['link_errors'])
                self.assertEqual(errors, link.errors)


if __name__ == '__main__':
    unittest.main()
