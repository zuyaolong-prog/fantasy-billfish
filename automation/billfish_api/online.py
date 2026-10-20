"""Bounded request/reply client for Billfish's current-user named pipe host."""

import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import time
import uuid

from .errors import ApiError

MAX_RESPONSE = 16 * 1024 * 1024
MAX_REQUEST = 1024 * 1024


class _Overlapped(ctypes.Structure):
    _fields_ = [("Internal", ctypes.c_size_t), ("InternalHigh", ctypes.c_size_t),
                ("Offset", wintypes.DWORD), ("OffsetHigh", wintypes.DWORD),
                ("hEvent", wintypes.HANDLE)]


class _PipeConnection:
    """Win32 byte pipe. Peek before reading so a silent host cannot block forever."""

    def __init__(self, name, deadline):
        if os.name != "nt":
            raise ApiError("online_unavailable", "Online Billfish transport requires Windows.")
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        self.api.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        self.api.CreateFileW.restype = wintypes.HANDLE
        self.api.CloseHandle.argtypes = [wintypes.HANDLE]
        self.api.CreateEventW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
        self.api.CreateEventW.restype = wintypes.HANDLE
        self.api.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.api.CancelIoEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p]
        self.api.GetOverlappedResult.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD), wintypes.BOOL]
        self.api.PeekNamedPipe.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
        self.api.ReadFile.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
        self.api.WriteFile.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
        self.handle = None
        while time.monotonic() < deadline:
            handle = self.api.CreateFileW("\\\\.\\pipe\\" + name, 0xC0000000, 0, None, 3, 0x40000000, None)
            if handle != ctypes.c_void_p(-1).value:
                self.handle = handle
                return
            error = ctypes.get_last_error()
            if error not in (2, 231):
                raise ApiError("online_unavailable", "Cannot connect to the Billfish online host.", {"winerror": error})
            time.sleep(min(0.02, max(0, deadline - time.monotonic())))
        raise ApiError("online_timeout", "Timed out connecting to the Billfish online host.")

    def _transfer(self, function, buffer, size, deadline):
        event = self.api.CreateEventW(None, True, False, None)
        if not event:
            raise ApiError("online_unavailable", "Cannot create an online transport event.")
        overlapped = _Overlapped(hEvent=event)
        transferred = wintypes.DWORD()
        pending = False
        try:
            if not function(self.handle, buffer, size, ctypes.byref(transferred), ctypes.byref(overlapped)):
                if ctypes.get_last_error() != 997:
                    raise ApiError("online_unavailable", "The Billfish online connection failed.")
                pending = True
                remaining = max(0, deadline - time.monotonic())
                result = self.api.WaitForSingleObject(event, max(1, int(remaining * 1000)))
                if result == 258:
                    raise ApiError("online_timeout", "Billfish online request timed out. Read back affected state before retrying a mutation.")
                if result != 0 or not self.api.GetOverlappedResult(self.handle, ctypes.byref(overlapped), ctypes.byref(transferred), False):
                    raise ApiError("online_unavailable", "The Billfish online connection failed.")
                pending = False
            return transferred.value
        finally:
            if pending:
                self.api.CancelIoEx(self.handle, ctypes.byref(overlapped))
                self.api.GetOverlappedResult(self.handle, ctypes.byref(overlapped), ctypes.byref(transferred), True)
            self.api.CloseHandle(event)

    def exchange(self, request, deadline, max_response):
        buffer = ctypes.create_string_buffer(request)
        if self._transfer(self.api.WriteFile, buffer, len(request), deadline) != len(request):
            raise ApiError("online_unavailable", "The Billfish online host disconnected while sending the request.")
        output = bytearray()
        while time.monotonic() < deadline:
            available = wintypes.DWORD()
            if not self.api.PeekNamedPipe(self.handle, None, 0, None, ctypes.byref(available), None):
                raise ApiError("online_unavailable", "The Billfish online host disconnected before replying.")
            if not available.value:
                time.sleep(min(0.01, max(0, deadline - time.monotonic())))
                continue
            size = min(available.value, 65536)
            buffer = ctypes.create_string_buffer(size)
            read = self._transfer(self.api.ReadFile, buffer, size, deadline)
            output.extend(buffer.raw[:read])
            if len(output) > max_response:
                raise ApiError("online_protocol_error", "Billfish online reply exceeds the response size limit.",
                               {"response_too_large": True, "limit_bytes": max_response})
            if b"\n" in output:
                return bytes(output)
        raise ApiError("online_timeout", "Timed out waiting for Billfish. Read back affected state before retrying a mutation.")

    def close(self):
        if self.handle is not None:
            self.api.CloseHandle(self.handle)
            self.handle = None


class OnlineClient:
    def __init__(self, install_root=None):
        self.install_root = Path(install_root) if install_root else Path(__file__).resolve().parents[2]

    def _descriptor(self):
        local = os.environ.get("LOCALAPPDATA")
        if not local:
            raise ApiError("online_unavailable", "LOCALAPPDATA is unavailable; cannot discover the Billfish online host.")
        path = Path(local) / "Billfish" / "automation" / "host.json"
        try:
            with path.open("rb") as stream:
                raw = stream.read(65537)
            if len(raw) > 65536:
                raise ValueError("Descriptor too large.")
            descriptor = json.loads(raw.decode("utf-8-sig"))
            if not isinstance(descriptor, dict) or type(descriptor.get("protocol")) is not int or descriptor["protocol"] != 1:
                raise ValueError("Unsupported host protocol.")
            pid = descriptor.get("pid")
            if type(pid) is not int or not 0 < pid <= 0xFFFFFFFF:
                raise ValueError("Invalid host PID.")
            if descriptor.get("pipe_name") != f"BillfishAutomation-v1-{pid}":
                raise ValueError("Invalid host pipe name.")
            root = descriptor.get("install_root")
            if not isinstance(root, str) or os.path.normcase(os.path.abspath(root)) != os.path.normcase(os.path.abspath(self.install_root)):
                raise ValueError("Online host belongs to a different Billfish installation.")
            return descriptor
        except (OSError, UnicodeError, ValueError, RecursionError) as exc:
            raise ApiError("online_unavailable", "No valid Billfish online host descriptor is available.", {"reason": str(exc)}) from exc

    def available(self):
        """Descriptor availability only. Use host.info to verify a live host."""
        try:
            self._descriptor()
            return True
        except ApiError:
            return False

    def call(self, operation, arguments, library_root=None, timeout=120, request_id=None):
        if not isinstance(operation, str) or not operation or not isinstance(arguments, dict):
            raise ApiError("invalid_arguments", "Online calls require an operation string and arguments object.")
        if isinstance(timeout, bool) or not isinstance(timeout, (int, float)) or not 0 < timeout <= 3600:
            raise ApiError("invalid_arguments", "Online timeout must be between 0 and 3600 seconds.")
        identifier = str(uuid.uuid4()) if request_id is None else request_id
        if not isinstance(identifier, str) or not identifier or len(identifier) > 128:
            raise ApiError("invalid_arguments", "request_id must be a nonempty string of at most 128 characters.")
        request = {"id": identifier, "operation": operation, "arguments": arguments}
        if library_root is not None:
            if not isinstance(library_root, (str, Path)):
                raise ApiError("invalid_arguments", "library_root must be a path string.")
            request["library_root"] = str(library_root)
        try:
            raw = (json.dumps(request, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")
        except (TypeError, ValueError, UnicodeError, RecursionError) as exc:
            raise ApiError("invalid_arguments", "Online request must be valid UTF-8 JSON.") from exc
        if len(raw) > MAX_REQUEST:
            raise ApiError("invalid_arguments", "Online request exceeds 1 MiB.")
        descriptor = self._descriptor()
        deadline = time.monotonic() + timeout
        max_response = MAX_RESPONSE if operation in {"diagnostics.objects", "items.selected", "ui.snapshot", "ui.capture"} else MAX_REQUEST
        try:
            connection = _PipeConnection(descriptor["pipe_name"], deadline)
        except ApiError as exc:
            details = dict(exc.details) if isinstance(exc.details, dict) else {"transport_details": exc.details} if exc.details is not None else {}
            details.update(request_id=identifier, job_id=identifier)
            raise ApiError(exc.code, exc.message, details, status=exc.status) from exc
        try:
            try:
                reply = connection.exchange(raw, deadline, max_response)
            except ApiError as exc:
                details = dict(exc.details) if isinstance(exc.details, dict) else {"transport_details": exc.details} if exc.details is not None else {}
                details.update(request_id=identifier, job_id=identifier)
                if operation == "items.selected" and details.get("response_too_large"):
                    raise ApiError("selection_too_large", "The current selection exceeds the 16 MiB response limit. Select fewer items and retry.", details) from exc
                raise ApiError(exc.code,
                               exc.message + " Request outcome is unknown; query jobs.get with this job_id and read back affected state before retrying. The client did not retry automatically.",
                               details, status=exc.status) from exc
        finally:
            connection.close()
        if len(reply) > max_response and operation == "items.selected":
            raise ApiError("selection_too_large", "The current selection exceeds the 16 MiB response limit. Select fewer items and retry.",
                           {"limit_bytes": max_response, "request_id": identifier, "job_id": identifier})
        try:
            if len(reply) > max_response or not reply.endswith(b"\n") or reply.count(b"\n") != 1:
                raise ValueError("Expected one bounded response frame.")
            def pairs(values):
                obj = {}
                for key, value in values:
                    if key in obj:
                        raise ValueError("Duplicate response key.")
                    obj[key] = value
                return obj
            body = json.loads(reply.decode("utf-8"), object_pairs_hook=pairs,
                              parse_constant=lambda value: (_ for _ in ()).throw(ValueError("Invalid number.")))
            if not isinstance(body, dict) or body.get("id") != identifier or type(body.get("ok")) is not bool:
                raise ValueError("Response identity or envelope mismatch.")
            if body["ok"]:
                if "data" not in body or "error" in body:
                    raise ValueError("Invalid success envelope.")
                return body["data"]
            error = body.get("error")
            if not isinstance(error, dict) or not isinstance(error.get("code"), str) or not isinstance(error.get("message"), str):
                raise ValueError("Invalid error envelope.")
        except (ValueError, UnicodeError, RecursionError) as exc:
            raise ApiError("online_protocol_error", "Billfish returned an invalid online response. Request outcome is unknown; query jobs.get with this job_id before retrying. The client did not retry automatically.",
                           {"reason": str(exc), "request_id": identifier, "job_id": identifier}) from exc
        host_details = error.get("details")
        details = dict(host_details) if isinstance(host_details, dict) else {"host_details": host_details} if host_details is not None else {}
        details.update(request_id=identifier, job_id=identifier)
        raise ApiError(error["code"], error["message"], details, status=400)
