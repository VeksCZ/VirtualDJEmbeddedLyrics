#!/usr/bin/env python3
"""Compatibility entry point for the LRC library tool."""

from lrc_tool import *  # noqa: F401,F403
from lrc_tool import main


if __name__ == "__main__":
    raise SystemExit(main())
