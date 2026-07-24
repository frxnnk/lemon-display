import os
import sys
import unittest


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))


class StudioJobsTest(unittest.TestCase):
    def test_inline_job_records_logs_progress_and_result(self):
        from studio.jobs import JobRunner

        runner = JobRunner(run_async=False)

        def task(payload, ctx):
            ctx.log("started")
            ctx.progress(42)
            return {"echo": payload["value"]}

        job = runner.submit("detect", task, {"value": "COM9"})

        self.assertEqual(job["state"], "complete")
        self.assertEqual(job["progress"], 100)
        self.assertEqual(job["logs"], ["started"])
        self.assertEqual(job["result"], {"echo": "COM9"})

    def test_cancel_before_run_marks_job_cancelled(self):
        from studio.jobs import JobRunner

        runner = JobRunner(run_async=False, autorun=False)
        job = runner.submit("build", lambda payload, ctx: {"ok": True})

        self.assertTrue(runner.cancel(job["id"]))
        runner.run(job["id"])

        updated = runner.get(job["id"])
        self.assertEqual(updated["state"], "cancelled")
        self.assertEqual(updated["progress"], 0)

    def test_failed_job_captures_error(self):
        from studio.jobs import JobRunner

        runner = JobRunner(run_async=False)

        def task(payload, ctx):
            raise RuntimeError("boom")

        job = runner.submit("flash", task)

        self.assertEqual(job["state"], "failed")
        self.assertIn("boom", job["error"])


if __name__ == "__main__":
    unittest.main()
