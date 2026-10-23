# The texts of the playable slice in English (milestone 9 bis, part 5), translated from
# locales/fr/tranche.ftl.

## Common words

common-yes = yes
choice-yes = Yes
choice-no = No
common-no = no
common-back = Back
common-keep = Keep
common-cancel = Cancel
common-apply = Apply
common-percent = { NUMBER($value, style: "percent") }
date-time = { $month }/{ $day }/{ $year } { $hour }:{ $minute }

## Title screen

game-title = Playable slice
title-subtitle = Title, loading, the map with a hero, pause.
title-help = Arrows and Enter, the pad's D-pad and A, or the mouse.
title-error = Failed: { $error }
title-play = Play
title-continue = Continue
title-load = Load
title-options = Options
title-quit = Quit

## Loading

loading-title = Loading
loading-world = Building the world
loading-files = Reading the files
loading-barrel = Barrel model
loading-decor = Cobblestones and street lamps
loading-characters = Animated characters
loading-sounds = Sounds

## Pause

pause-title = Pause
pause-frozen = The world is frozen underneath.
pause-help = Esc or Start: resume
pause-resume = Resume
pause-save = Save
pause-options = Options
pause-quit = Back to the title

## Saves

slots-save-title = Save the game
slots-load-title = Load a game
slots-help = Arrows and Enter; Esc: back
slots-saved = Saved in { $slot }.
slots-failed = Failed: { $error }
slot-manual = Slot { $number }
slot-auto = Autosave { $number }
slot-empty = (empty)
slot-unreadable = Unreadable: { $error }
slot-when = { $date }, { DURATION($seconds) } played
slot-when-backup = { $date }, { DURATION($seconds) } played (backup copy)
slot-summary = Health { NUMBER($health, style: "percent") }, { $creatures ->
        [one] { $creatures } creature standing
       *[other] { $creatures } creatures standing
    }
save-unreadable = Save “{ $slot }” unreadable: { $error }

## Options

options-title = Options
options-display = Display
options-audio = Audio
options-controls = Controls
options-language = Language

options-display-title = Display
options-display-help = Left and right arrows: change; Enter: apply
options-display-confirm-title = Keep these settings?
options-display-confirm-countdown = { $seconds ->
        [one] Back to the previous settings in { $seconds } second.
       *[other] Back to the previous settings in { $seconds } seconds.
    }
options-display-mode-label = Mode
options-display-screen-label = Screen
options-display-screen-number = Screen { $number }
options-display-vsync-label = Vertical sync
options-display-frame-limit-label = Frame limit
options-display-frame-limit = { $limit ->
        [0] None
       *[other] { $limit } per second
    }
options-display-render-scale-label = 3D resolution
options-display-anti-aliasing-label = Anti-aliasing
options-display-shadows-label = Shadows
options-display-ui-scale-label = Interface size
display-mode-fullscreen = Fullscreen
display-mode-windowed = Windowed
shadows-low = Low
shadows-medium = Medium
shadows-high = High
anti-aliasing-none = None

options-audio-title = Audio
options-audio-help = Left and right arrows: change
audio-volume = { NUMBER($volume, style: "percent") }
audio-master = Master volume
audio-music = Music
audio-effects = Effects
audio-ambience = Ambience
audio-interface = Interface

options-controls-title = Controls
controls-device-pad = Gamepad (the keyboard: confirm with the keyboard)
controls-device-keyboard = Keyboard and mouse (the gamepad: confirm with the gamepad)
controls-help = Enter: change the key
controls-press-pad = Press a button of the gamepad.
controls-press-keyboard = Press a key or a mouse button.
controls-cancel-pad = Start: cancel
controls-cancel-keyboard = Esc: cancel
controls-layout-label = Layout
controls-reset = Restore the default keys
controls-reset-done = Default keys restored.
controls-taken = { $key } removed from: { $actions }
controls-save-failed = The keys could not be saved.
input-profile-clic = Click to move
input-profile-zqsd = Keyboard movement (WASD)

options-language-title = Language
options-language-help = The change applies at once.

## Actions (the game's commands)

action-move-to = Move (by clicking)
action-forward = Forward
action-backward = Backward
action-left = Left
action-right = Right
action-skill = Skill { $number }
action-zoom-in = Zoom in
action-zoom-out = Zoom out
action-select = Select a creature
action-next = Next creature
action-follow = Follow the creature
action-camera-up = Camera: up
action-camera-down = Camera: down
action-camera-left = Camera: left
action-camera-right = Camera: right
action-pause = Pause

## In game

flash-hit = Hit!
flash-skill = Skill { $number }
hud-world = 3D demo: map { $width }×{ $height }, { $creatures } creatures, { $objects } fixed objects, { $torches } torches
hud-scene = scene: { $meshes } / { $meshes_submitted } meshes, { $triangles } triangles, { $draws } draw calls; shadow: { $casters } / { $casters_submitted }, { $shadow_draws } draw calls
hud-lights = torches: { $shadowed } shadowed, { $updated } updated, { $light_draws } draw calls; billboards: { $billboards }; draw calls in all: { $draws }
hud-move = { $layout }: move { $keys }, skills { $skills }
hud-hero = { $move } on a nearby creature, or { $strike }: strike it (blows: { $hits }). { $follow }: follow camera ({ $following }), { $pause }: pause
hud-cell = Cell ({ $x }, { $y }).
hud-observer = { $cell }{ $select }: select a creature, { $next }: the next one, { $follow }: follow it ({ $following })

## The engine's messages the game replaces (crash report, lost graphics card)

moteur-crash-message =
    The game ran into an error and has to close.

    A report was written here:
    { $report }

    Attaching this file (and the .dmp next to it) to a bug report helps fix the problem.
moteur-gpu-lost-message =
    The graphics card stopped responding (its driver was reset, or the card was removed), and the game has to close.

    Updating the graphics driver usually fixes it.

    A report was written here:
    { $report }
