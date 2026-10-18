"""Minimal stand-in so Ren'Py's `setup.py generate` runs on a host Python without
setuptools. `generate` only records Extension objects and returns before setup()."""


class Extension:
    def __init__(self, name, sources, **kwargs):
        self.name = name
        self.sources = sources


def setup(**kwargs):
    raise SystemExit("hoststub setuptools: only `setup.py generate` is supported")


class SetuptoolsDeprecationWarning(Warning):
    pass
