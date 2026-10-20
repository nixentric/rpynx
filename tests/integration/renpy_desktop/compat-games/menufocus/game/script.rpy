# Main menu focus (rsl_bridge._focus_first_button): the top button is selected
# when the main menu appears, without a first D-pad press.
init python:
    def report_focus():
        import os
        f = renpy.display.focus.get_focused()
        label = f.child.text[0] if f is not None else None
        with open(os.environ["RSL_RESULT"], "w") as out:
            out.write("focused=%r mouse=%r\n" % (label, renpy.display.interface.is_mouse_visible()))
        renpy.quit()

screen main_menu():
    tag menu
    vbox:
        align (0.5, 0.5)
        textbutton "First" action NullAction()
        textbutton "Second" action NullAction()
    # Declared later but drawn higher, like a top bar link: must not win.
    textbutton "Top bar link" action NullAction() align (0.5, 0.0)
    timer 3.0 action Function(report_focus)

label start:
    return

label splashscreen:
    call screen main_menu
    return
