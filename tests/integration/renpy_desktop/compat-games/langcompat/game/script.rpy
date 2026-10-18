# Synthetic repro of the Forbidden Fantasy pattern: a gui value set by define,
# then changed in init python; a language change must not reset it (7.5.3 rule).
define gui.btn = 0
init python:
    gui.btn = 40

label splashscreen:
    python:
        import os
        before = gui.btn
        renpy.change_language("french")
        with open(os.environ["RSL_RESULT"], "w") as f:
            f.write("script_version=%r before=%r after=%r\n" % (config.script_version, before, gui.btn))
        renpy.quit()
