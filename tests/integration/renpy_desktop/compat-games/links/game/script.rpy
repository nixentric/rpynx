# A website link (OpenURL) and Forbidden Fantasy's folder button (subprocess.Popen
# of xdg-open): on the Switch neither can open. The bridge shows a notice with an OK
# button instead of doing nothing (link) or stopping the game (folder, run 24).
init python:
    import subprocess
    seen = []

    def open_deluxe_folder():   # as Forbidden Fantasy does it: the result is not returned
        subprocess.Popen(["xdg-open", "/game/deluxe"])

    def sample():
        shown = renpy.get_screen("_rsl_cannot_open") is not None
        f = renpy.display.focus.get_focused()
        if shown and f is None:
            return
        state = "shown, OK selected" if shown and isinstance(getattr(f, "action", None), Hide) else \
            ("shown, %s selected" % type(getattr(f, "action", None)).__name__ if shown else "hidden")
        if not seen or seen[-1] != state:
            seen.append(state)

    def report():
        import os
        with open(os.environ["RSL_RESULT"], "w") as out:
            out.write("seen=%r\n" % (seen,))
        renpy.quit()

screen links():
    vbox:
        align (0.5, 0.5)
        spacing 20
        textbutton "Website" action OpenURL("https://example.com/{page}")
        textbutton "Deluxe folder" action Function(open_deluxe_folder)
    timer 0.2 repeat True action Function(sample)
    timer 11.0 action Function(report)

label start:
    return

label splashscreen:
    show screen links
    $ renpy.pause(hard=True)
    return
