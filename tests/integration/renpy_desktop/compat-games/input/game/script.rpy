# The Switch keyboard fix (rsl_keyboard.install): a text field must
# finish without a hardware Enter key; A does not submit a Ren'Py input.
label splashscreen:
    $ name = renpy.input("Name?", default="Lucas")
    python:
        import os
        with open(os.environ["RSL_RESULT"], "w") as f:
            f.write("name=%r\n" % name)
        renpy.quit()
