# Forbidden Fantasy's outfit screen (skins_menu_b_characters): cards are
# `button at skin_opacity` with a frame child; the action shows the next
# screen. D-pad selects a card; A must activate it (hardware run 16).
transform skin_opacity:
    on hover:
        linear 0.3 alpha 1.0
    on idle:
        linear 0.3 alpha 0.5

init python:
    picked = None

    def pick(name):
        global picked
        picked = name

    def report():
        import os
        f = renpy.display.focus.get_focused()
        with open(os.environ["RSL_RESULT"], "w") as out:
            out.write("picked=%r focused=%r\n" % (picked, type(f).__name__ if f else None))
        renpy.quit()

screen next_screen():
    text "next" align (0.5, 0.5)
    timer 1.0 action Function(report)

screen cards():
    text "Choose their clothes" xalign 0.5 yalign 0.1
    hbox:
        align (0.5, 0.5)
        spacing 40
        for name in ("Elven Archer", "Hermes Blessing"):
            button at skin_opacity:
                action [Function(pick, name), Show("next_screen"), Hide("cards")]
                frame:
                    background Solid("#00000000")
                    maximum 300, 400
                    add Solid("#888", xysize=(280, 360))
                    text name align (0.5, 0.95)
    timer 8.0 action Function(report)

label start:
    return

label splashscreen:
    show screen cards
    $ renpy.pause(hard=True)
    return
