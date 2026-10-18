# Forbidden Fantasy's layout (hardware run 18): an always-on debug_screen with
# no buttons, and a quick menu registered in config.overlay_screens whose "Back"
# is drawn before the content. A must advance dialogue and pick the first card;
# "Back" must never be selected for the player. RSL_PART=dialogue|cards.
init python:
    config.overlay_screens.append("quick_menu")

    def report(what):
        import os
        with open(os.environ["RSL_RESULT"], "w") as out:
            out.write("%s\n" % what)
        renpy.quit()

screen say(who, what):
    text what id "what" align (0.5, 0.9)

screen debug_screen():
    zorder 100
    text "debug" align (1.0, 0.0)

screen quick_menu():
    zorder 0
    textbutton "Back" action Function(report, "pressed Back") align (0.0, 1.0)

screen cards():
    zorder 50
    hbox:
        align (0.5, 0.5)
        spacing 40
        textbutton "Elven Archer" action Function(report, "picked Elven Archer")
        textbutton "Hermes Blessing" action Function(report, "picked Hermes Blessing")

# Run in the game context, where Ren'Py shows config.overlay_screens (it does
# not in splashscreen); returning from a main_menu label starts the game.
label main_menu:
    return

label start:
    show screen debug_screen
    if os.environ.get("RSL_PART") == "cards":
        show screen cards
        $ renpy.pause(30, hard=True)
    $ renpy.pause(1.5, hard=True)   # between lines, dialogue window hidden
    "Line one."
    $ report("advanced")

init python:
    import os
