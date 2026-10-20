"""Regenerate the checked-in OpenAPI document from the shared operation registry."""
import json
from pathlib import Path

from billfish_api.service import Service


if __name__ == "__main__":
    destination = Path(__file__).with_name("openapi.json")
    destination.write_text(json.dumps(Service().openapi(), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(destination)
