"""Errors shared by the CLI, HTTP API and MCP transports."""


class ApiError(Exception):
    def __init__(self, code, message, details=None, status=400):
        super().__init__(message)
        self.code = code
        self.message = message
        self.details = details
        self.status = status

    def to_dict(self):
        error = {"code": self.code, "message": self.message}
        if self.details is not None:
            error["details"] = self.details
        return error
