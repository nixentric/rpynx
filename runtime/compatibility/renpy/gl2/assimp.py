# rpynx compatibility module. Replaces renpy/gl2/assimp.pyx.
#
# Ren'Py imports renpy.gl2.assimp unconditionally, but the Open Asset Importer
# (assimp, C++) is not available for the Nintendo Switch. Everything Ren'Py
# calls during normal 2D play is a no-op here; using a 3D model (GLTFModel)
# raises an error naming the limitation instead of failing obscurely.

import threading

import renpy
from renpy.display.displayable import Displayable

loader = None
"No assimp loader exists on the Switch."

loader_lock = threading.Lock()

UNSUPPORTED = (
    "GLTFModel is not supported by rpynx: the assimp 3D model "
    "library is not available on the Nintendo Switch."
)


def free_memory():
    pass


def finish_predict():
    pass


def preload():
    pass


class GLTFModel(Displayable):

    def __init__(self, filename, *args, **kwargs):
        raise Exception(UNSUPPORTED + " (model: {!r})".format(filename))
