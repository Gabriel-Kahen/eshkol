"""Check SSE acceptance assertions with a fake writer and a scripted clock.

Run with Python 3. The writer model and its deliberate faults are independent
of the product. The eventual scaffold should capture real send times and bytes
and apply the same assertions to the server. Times below are monotonic offsets
from frame admission, and wire bytes exclude the HTTP response headers.
"""

from collections import deque
import unittest


WIRE = b"1f\r\ndata: caf\xc3\xa9\ndata: second line\n\n\r\n"
FIRST_WRITE = 14
DEADLINE_MS = 5_000


class ScriptedWriter:
    def __init__(self, *limits):
        self.limits = deque(limits)
        self.attempts = []
        self.wire = bytearray()

    def send(self, now_ms, suffix):
        self.attempts.append((now_ms, suffix))
        limit = self.limits.popleft() if self.limits else len(suffix)
        if limit is None:
            raise BlockingIOError
        accepted = suffix[:limit]
        self.wire.extend(accepted)
        return len(accepted)


class FrameModel:
    def __init__(self, writer):
        self.writer = writer
        self.offset = 0

    def progress(self, now_ms):
        if now_ms >= DEADLINE_MS:
            return "timeout"
        return self.write(now_ms)

    def write(self, now_ms):
        suffix = WIRE[self.write_offset():]
        try:
            self.offset = self.write_offset() + self.writer.send(now_ms, suffix)
        except BlockingIOError:
            return "pending"
        return "complete" if self.offset == len(WIRE) else "pending"

    def write_offset(self):
        return self.offset


class RestartAtZero(FrameModel):
    def write_offset(self):
        return 0


class RenewDeadlineOnRetry(FrameModel):
    def progress(self, now_ms):
        # Each wake treats its own time as a fresh admission.
        return self.write(now_ms)


class ReportTimeoutAfterWrite(FrameModel):
    def progress(self, now_ms):
        result = self.write(now_ms)
        return "timeout" if now_ms >= DEADLINE_MS else result


class WriteControls(unittest.TestCase):
    def check_short_write(self, candidate):
        writer = ScriptedWriter(FIRST_WRITE, None, None)
        frame = candidate(writer)
        self.assertEqual(
            [frame.progress(ms) for ms in (0, 10, 20, 30)],
            ["pending", "pending", "pending", "complete"],
        )
        self.assertEqual(bytes(writer.wire), WIRE, "emitted wire bytes differ")
        self.assertEqual(
            [suffix for _, suffix in writer.attempts],
            [WIRE, WIRE[FIRST_WRITE:], WIRE[FIRST_WRITE:], WIRE[FIRST_WRITE:]],
            "retry offered the wrong suffix",
        )

    def check_deadline(self, candidate, first_limit):
        writer = ScriptedWriter(first_limit, None, None)
        frame = candidate(writer)
        for now_ms in (0, 4_000, 4_999):
            self.assertEqual(frame.progress(now_ms), "pending")
        self.assertEqual(frame.progress(DEADLINE_MS), "timeout", "deadline moved")
        self.assertEqual(
            [time for time, _ in writer.attempts],
            [0, 4_000, 4_999],
            "send attempted at or after deadline",
        )
        self.assertEqual(bytes(writer.wire), WIRE[:first_limit or 0])

    def test_short_write(self):
        self.assertEqual(WIRE[FIRST_WRITE - 1:FIRST_WRITE + 1], b"\xc3\xa9")
        self.check_short_write(FrameModel)

    def test_deadline_with_and_without_partial_progress(self):
        for first_limit in (None, FIRST_WRITE):
            with self.subTest(first_limit=first_limit):
                self.check_deadline(FrameModel, first_limit)

    def test_catches_offset_reset_even_if_frame_reports_complete(self):
        with self.assertRaisesRegex(AssertionError, "emitted wire bytes differ"):
            self.check_short_write(RestartAtZero)

    def test_catches_deadline_renewal(self):
        with self.assertRaisesRegex(AssertionError, "deadline moved"):
            self.check_deadline(RenewDeadlineOnRetry, FIRST_WRITE)

    def test_catches_late_write_even_if_timeout_status_is_correct(self):
        with self.assertRaisesRegex(AssertionError, "send attempted at or after deadline"):
            self.check_deadline(ReportTimeoutAfterWrite, FIRST_WRITE)


if __name__ == "__main__":
    unittest.main(verbosity=2)
