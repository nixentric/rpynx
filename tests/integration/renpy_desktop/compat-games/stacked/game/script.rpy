# Forbidden Fantasy's main menu: buttons taller than the space between them, so
# each box overlaps the next. Plain Ren'Py's D-pad down from "New Game" skips
# "Continue", whose box starts above New Game's bottom edge (hardware run 24).
init python:
    picked = None

    def pick(name):
        global picked
        picked = name
        report()

    def report():
        import os
        with open(os.environ["RSL_RESULT"], "w") as out:
            out.write("picked=%r\n" % (picked,))
        renpy.quit()

screen stacked():
    vbox:
        align (0.2, 0.6)
        spacing -30
        for name in ("New Game", "Continue", "Supporter Content"):
            button:
                xysize (300, 90)
                background Solid("#333")
                hover_background Solid("#666")
                action Function(pick, name)
                text name align (0.5, 0.5)
    timer 8.0 action Function(report)

label start:
    return

label splashscreen:
    show screen stacked
    $ renpy.pause(hard=True)
    return
