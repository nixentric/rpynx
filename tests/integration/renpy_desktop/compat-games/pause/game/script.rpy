# A pause with only the quick menu up: A must continue the pause, not press the
# quick menu's Back (rsl_focus.focus_first_button leaves always-up screens alone).
init python:
    def report(what):
        import os
        with open(os.environ["RSL_RESULT"], "w") as out:
            out.write("%s\n" % what)
        renpy.quit()

screen quick_menu():
    zorder 100
    textbutton "Back" action Function(report, "pressed Back") align (0.0, 1.0)

label start:
    return

label splashscreen:
    show screen quick_menu
    $ renpy.pause(30)
    $ report("continued")
