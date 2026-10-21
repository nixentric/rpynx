"""Diagnostics for hardware tests, for rsl_bridge: saves and loads go to python.txt
with the script line, so a test can check from the log that a load returns to the
line that was saved (runs 25-26)."""
import renpy

_report_load = False


def where():
    """file:line of the game's current statement (below any menu context)."""
    try:
        node = renpy.game.script.lookup(renpy.game.contexts[0].current)
        return "%s:%d" % (node.filename, node.linenumber)
    except Exception as e:   # a diagnostic must never break a save or load
        return "unknown (%r)" % e


def _after_load():
    # Ren'Py runs this inside its _after_load label (00start.rpy), so the game's line is
    # only current at the next interaction: report_load() logs it there (run 25 logged 00start).
    global _report_load
    _report_load = True


def report_load():
    """Called by rsl_bridge.step() at every poll, so at the first interaction after a load."""
    global _report_load
    if _report_load:
        _report_load = False
        print("rsl: loaded, now at %s" % where(), flush=True)


def install():
    """Runs from rsl_bridge's display setup, once the game's config exists."""
    loadsave = renpy.loadsave
    if not getattr(loadsave.save, "_rsl", False):
        save, load = loadsave.save, loadsave.load

        def logged_save(slotname, *args, **kwargs):
            print("rsl: save %s at %s" % (slotname, where()), flush=True)
            return save(slotname, *args, **kwargs)

        def logged_load(filename):
            print("rsl: load %s" % filename, flush=True)
            return load(filename)
        logged_save._rsl = True
        loadsave.save = renpy.exports.save = logged_save   # renpy.save in game code is renpy.exports.save
        loadsave.load = renpy.exports.load = logged_load
    if _after_load not in renpy.config.after_load_callbacks:   # 00start.rpy resets the list at init
        renpy.config.after_load_callbacks.append(_after_load)
