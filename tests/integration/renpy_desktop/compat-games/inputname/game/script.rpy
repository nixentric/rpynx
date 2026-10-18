# A name screen like Forbidden Fantasy's (inputname): a returnable field plus
# its own OK and "Use default name" buttons. After the keyboard the player
# must see the name and press OK themselves (hardware run 16).
default name_input_field = "Syd"

screen inputname():
    vbox:
        align (0.5, 0.5)
        input value VariableInputValue("name_input_field", returnable=True)
        textbutton "OK" action Return(name_input_field)
        textbutton "Use default name" action Return("Syd")

label start:
    return

label splashscreen:
    $ name = renpy.call_screen("inputname")
    python:
        import os
        with open(os.environ["RSL_RESULT"], "w") as f:
            f.write("name=%r\n" % name)
        renpy.quit()
