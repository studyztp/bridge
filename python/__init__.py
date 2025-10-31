"""Python package for bridge bindings.

This package exposes a compiled extension module named `_bridge` built from
the pybind11 wrapper. After building, you can `from bridge import _bridge` or
import helpers from this package.
"""

from . import _bridge  # re-export compiled module

__all__ = ["_bridge"]
