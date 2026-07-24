import threading
import time
import traceback
import uuid


class JobContext:
    def __init__(self, runner, job_id):
        self._runner = runner
        self._job_id = job_id

    @property
    def cancelled(self):
        job = self._runner.get(self._job_id)
        return bool(job and job.get("cancelRequested"))

    def log(self, message):
        self._runner._append_log(self._job_id, str(message))

    def progress(self, value):
        self._runner._set_progress(self._job_id, value)


class JobRunner:
    def __init__(self, run_async=True, autorun=True):
        self.run_async = run_async
        self.autorun = autorun
        self._jobs = {}
        self._handlers = {}
        self._lock = threading.RLock()

    def register(self, kind, handler):
        self._handlers[kind] = handler

    def submit(self, kind, handler=None, payload=None):
        target = handler or self._handlers.get(kind)
        if not target:
            raise ValueError(f"no handler registered for job kind: {kind}")

        now = time.time()
        job_id = uuid.uuid4().hex[:12]
        with self._lock:
            self._jobs[job_id] = {
                "id": job_id,
                "kind": kind,
                "state": "queued",
                "progress": 0,
                "logs": [],
                "result": None,
                "error": None,
                "createdAt": now,
                "updatedAt": now,
                "startedAt": None,
                "completedAt": None,
                "cancelRequested": False,
                "_handler": target,
                "_payload": payload or {},
            }

        if self.autorun:
            if self.run_async:
                thread = threading.Thread(target=self.run, args=(job_id,), daemon=True)
                thread.start()
            else:
                self.run(job_id)
        return self.get(job_id)

    def run(self, job_id):
        with self._lock:
            job = self._jobs.get(job_id)
            if not job:
                return None
            if job["cancelRequested"]:
                job["state"] = "cancelled"
                job["completedAt"] = time.time()
                job["updatedAt"] = job["completedAt"]
                return self._public(job)
            if job["state"] != "queued":
                return self._public(job)
            job["state"] = "running"
            job["startedAt"] = time.time()
            job["updatedAt"] = job["startedAt"]
            handler = job["_handler"]
            payload = dict(job["_payload"])

        ctx = JobContext(self, job_id)
        try:
            result = handler(payload, ctx)
            with self._lock:
                job = self._jobs[job_id]
                if job["cancelRequested"]:
                    job["state"] = "cancelled"
                else:
                    job["state"] = "complete"
                    job["progress"] = 100
                    job["result"] = result
                job["completedAt"] = time.time()
                job["updatedAt"] = job["completedAt"]
        except Exception as exc:
            with self._lock:
                job = self._jobs[job_id]
                job["state"] = "failed"
                job["error"] = str(exc)
                job["logs"].append(traceback.format_exc(limit=6).strip())
                job["completedAt"] = time.time()
                job["updatedAt"] = job["completedAt"]
        return self.get(job_id)

    def cancel(self, job_id):
        with self._lock:
            job = self._jobs.get(job_id)
            if not job or job["state"] in {"complete", "failed", "cancelled"}:
                return False
            job["cancelRequested"] = True
            job["updatedAt"] = time.time()
            return True

    def get(self, job_id):
        with self._lock:
            job = self._jobs.get(job_id)
            return self._public(job) if job else None

    def list(self):
        with self._lock:
            jobs = [self._public(job) for job in self._jobs.values()]
        return sorted(jobs, key=lambda j: j["createdAt"], reverse=True)

    def _append_log(self, job_id, message):
        with self._lock:
            job = self._jobs[job_id]
            job["logs"].append(message)
            job["updatedAt"] = time.time()

    def _set_progress(self, job_id, value):
        with self._lock:
            job = self._jobs[job_id]
            job["progress"] = max(0, min(100, int(value)))
            job["updatedAt"] = time.time()

    def _public(self, job):
        if not job:
            return None
        return {k: v for k, v in job.items() if not k.startswith("_")}

