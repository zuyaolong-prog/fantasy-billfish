"""JSON command line adapter for the Billfish service."""

import argparse
import json
import sys
from pathlib import Path

from . import __version__
from .errors import ApiError
from .service import Service

VERSION = __version__


class HelpRequested(Exception):
    pass


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise ApiError("invalid_arguments", message, status=400)

    def print_help(self, file=None):
        raise HelpRequested(self.format_help())


def envelope(data=None, error=None):
    if error is None:
        return {"ok": True, "data": data}
    return {"ok": False, "error": {
        "code": error.code, "message": error.message,
        "details": error.details,
    }}


def build_parser():
    parser = Parser(prog="billfish-cli", description="Billfish local automation API. All results are JSON.")
    parser.add_argument("--library", help="Library root directory.")
    parser.add_argument("--version", action="store_true")
    commands = parser.add_subparsers(dest="command")
    for name in ("info", "selected", "folders", "tags", "schema", "openapi"):
        commands.add_parser(name)
    search = commands.add_parser("search")
    search.add_argument("query", nargs="?", default="")
    search.add_argument("--limit", type=int, default=100)
    search.add_argument("--offset", type=int, default=0)
    search.add_argument("--folder-id")
    search.add_argument("--tag-id")
    search.add_argument("--ext", action="append")
    item = commands.add_parser("item")
    item.add_argument("id")
    call = commands.add_parser("call")
    call.add_argument("operation")
    source = call.add_mutually_exclusive_group()
    source.add_argument("--json", dest="json_text")
    source.add_argument("--input", help="UTF-8 JSON file, or - for stdin.")
    serve = commands.add_parser("serve")
    serve.add_argument("--port", type=int, default=27393)
    serve.add_argument("--token-file")
    return parser


def _arguments(args):
    try:
        if args.json_text is not None:
            raw = args.json_text
        elif args.input == "-":
            raw = sys.stdin.read()
        elif args.input:
            raw = Path(args.input).read_text(encoding="utf-8-sig")
        else:
            return {}
        def pairs(values):
            obj = {}
            for key, value in values:
                if key in obj:
                    raise ValueError("Duplicate object key.")
                obj[key] = value
            return obj
        def invalid_constant(value):
            raise ValueError("Non-finite numbers are not valid JSON.")
        value = json.loads(raw, object_pairs_hook=pairs, parse_constant=invalid_constant)
    except (OSError, UnicodeError, ValueError, RecursionError) as exc:
        raise ApiError("invalid_arguments", "Cannot read a JSON arguments object.", {"reason": str(exc)}) from exc
    if not isinstance(value, dict):
        raise ApiError("invalid_arguments", "Arguments must be a JSON object.")
    return value


def main(argv=None):
    # Windows redirected output must stay UTF-8, including Chinese library names.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    result = None
    exit_code = 0
    argument_phase = True
    try:
        args = build_parser().parse_args(argv)
        if args.version:
            result = envelope({"version": VERSION})
        else:
            if not args.command:
                raise ApiError("invalid_arguments", "A command is required. Use --help.")
            if args.command == "call":
                operation, arguments = args.operation, _arguments(args)
            elif args.command == "search":
                operation = "items.search"
                arguments = {key: getattr(args, key) for key in
                             ("query", "limit", "offset", "folder_id", "tag_id", "ext")
                             if getattr(args, key) is not None}
                if "ext" in arguments:
                    arguments["extensions"] = arguments.pop("ext")
            elif args.command == "item":
                operation, arguments = "items.get", {"item_id": args.id}
            else:
                operation = {"info": "library.info", "selected": "items.selected", "folders": "folders.list", "tags": "tags.list"}.get(args.command)
                arguments = {}
            service = Service(library=args.library)
            if args.command == "serve":
                from .http_server import load_token, serve
                token = load_token(args.token_file)
                if not 0 <= args.port <= 65535:
                    raise ApiError("invalid_arguments", "Port must be between 0 and 65535.")
                argument_phase = False
                serve(service, token, args.port)
                result = envelope({"stopped": True})
            else:
                argument_phase = False
                data = service.capabilities() if args.command == "schema" else service.openapi() if args.command == "openapi" else service.call(operation, arguments)
                result = envelope(data)
    except HelpRequested as exc:
        result = envelope({"help": str(exc)})
    except ApiError as exc:
        result = envelope(error=exc)
        exit_code = 2 if argument_phase or exc.code in {"invalid_argument", "invalid_arguments"} else 1
    except KeyboardInterrupt:
        result = envelope(error=ApiError(
            "interrupted",
            "Operation interrupted. Read back the affected state to confirm the outcome; changes may already be committed, rolled back, or partially copied.",
            status=499,
        ))
        exit_code = 130
    except Exception as exc:
        print(f"Billfish CLI error: {type(exc).__name__}: {exc}", file=sys.stderr)
        result = envelope(error=ApiError("internal_error", "An unexpected error occurred.", status=500))
        exit_code = 1
    print(json.dumps(result, ensure_ascii=False, allow_nan=False))
    return exit_code
