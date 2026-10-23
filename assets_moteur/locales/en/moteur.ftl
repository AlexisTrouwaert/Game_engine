# The engine's texts the player sees (milestone 9 bis, part 5): names of the controls, durations.
# A game may replace them with a file of the same name in its assets.

## Mouse and wheel

input-mouse-left = Left click
input-mouse-right = Right click
input-mouse-middle = Middle click
input-mouse-x1 = Mouse 4
input-mouse-x2 = Mouse 5
input-mouse-other = Mouse ?
input-wheel-up = Wheel up
input-wheel-down = Wheel down

## Gamepad (an Xbox pad's names, by position)

input-pad-a = Pad A
input-pad-b = Pad B
input-pad-x = Pad X
input-pad-y = Pad Y
input-pad-lb = Pad LB
input-pad-rb = Pad RB
input-pad-lt = Pad LT
input-pad-rt = Pad RT
input-pad-l3 = Pad L3
input-pad-r3 = Pad R3
input-pad-dpad-up = D-pad up
input-pad-dpad-down = D-pad down
input-pad-dpad-left = D-pad left
input-pad-dpad-right = D-pad right
input-pad-start = Pad Start
input-pad-select = Pad Select
input-pad-button = Pad { $name }
input-pad-axis = Pad { $name }
input-pad-left-stick = Left stick
input-pad-right-stick = Right stick
input-none = (none)

## Keys without a character (the key's SDL name, lower case, spaces as dashes)

input-key-space = Space
input-key-return = Enter
input-key-escape = Esc
input-key-tab = Tab
input-key-backspace = Backspace
input-key-delete = Delete
input-key-insert = Insert
input-key-home = Home
input-key-end = End
input-key-pageup = Page Up
input-key-pagedown = Page Down
input-key-up = Up
input-key-down = Down
input-key-left = Left
input-key-right = Right
input-key-left-shift = Left Shift
input-key-right-shift = Right Shift
input-key-left-ctrl = Left Ctrl
input-key-right-ctrl = Right Ctrl
input-key-left-alt = Alt
input-key-right-alt = Alt Gr
input-key-capslock = Caps Lock
input-key-keypad-enter = Enter (keypad)

## Durations (Localization::format_duration(), DURATION() in the texts)

moteur-duration-hours = { $hours } h { $minutes } min
moteur-duration-minutes = { $minutes } min { $seconds } s
moteur-duration-seconds = { $seconds } s
