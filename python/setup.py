from setuptools import setup
from pybind11.setup_helpers import Pybind11Extension, build_ext
from pathlib import Path

this_dir = Path(__file__).resolve().parent

sources = [
    str(this_dir / "bridge_py.cpp"),
    str(this_dir.parent / "bridge.cpp"),
]

ext_modules = [
    Pybind11Extension(
        "bridge._bridge",
        sources,
        include_dirs=[str(this_dir.parent)],
        extra_compile_args=["-std=c++17", "-fPIC", "-DDEBUG"],
        extra_link_args=["-pthread"],
        language="c++",
    )
]

setup(
    name="bridge",
    version="0.1",
    description="Python bindings for bridge library",
    packages=["bridge"],
    package_dir={"bridge": str(this_dir)},
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
)
