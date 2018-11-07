SDL\_DumpEvents

# Overview

This program initalizes SDL and uses SDL to display a report of input events, displayed in logfile sequence within categorized columns.
Columns are provided for Keyboard events, Mouse events, Joystick events, Game Controller events, and one catch-all for various (but not all) other events.
The main purpose is to identify what SDL input events are occurring, per device type.
The original design goal was to test various Steam Controller configurations.


## Keyboard events

Keyboard events reported include:

* KEYDOWN = a key was pressed
* KEYUP = a key was released


## Mouse events

Mouse events reported include:

* MOUSEMOTION = motion of the mouse, x and y changes as a 2-tuple.
* MOUSEBUTTONDOWN = press of a mouse button (1 = primary button)
* MOUSEBUTTONUP = release of a mouse button
* MOUSEWHEEL = scroll wheel motion, horizontal and vertical as 2-tuple.
    Vertical-only is the more common single mouse wheel motion on most mice.
    Trackpad "two-finger scrolling" may generate both horizontal and vertical.


## Joystick events

Joystick events reported include:

* JOYAXISMOTION = change in an axis, reporting joystick number, axis number, and current axis value.
* JOYHATMOTION = chnage in a hat, reporting joystick number, hat number, and current hat value.
* JOYBALLMOTION = change in a joystick's trackball motion, reporting joystick number, ball number, and x and y changes.
* JOYBUTTONDOWN = press of a joystick button
* JOYBUTTONUP = release of a joystick button
* JOYDEVICEADDED = new joystick attached to system
* JOYDEVICEREMOVED = joystick detached from system


## Controller events

Game controller events reported include:

* CONTROLLERAXISMOTION = change in an axis
* CONTROLLERBUTTONDOWN = press of a controller button
* CONTROLLERBUTTONUP = press of a controller button
* CONTROLLERDEVICEADDED = new controller attached to system
* CONTROLLERDEVICEREMOVED = controller detached from system
* CONTROLLERDEVICEREMAPPED = change in controller mapping (joystick-to-controller axis/button mapping)

If you are unfamiliar with the SDL Game Controller API, it operates on top of an existing SDL joystick device, and can be thought of as a means of mapping joystick events to Xbox 360 Controller events (more precisely, Microsoft XInput).
Carrying out such a mapping shifts the burden of identifying the plethora of different makes and models of joysticks to external operations, while the SDL-using software itself need only worry about a single/simple fixed set of Xbox360-style controls.
After the arrival of the Steam Controller in the Valve Steam client, the Game Controller mappings can also be thought of as a precursor to the Steam Controller mapping or a Steam-less substitute thereof.


## Miscellanous events

Miscellaneous events reported include:

* SHOWN = window became shown
* HIDDEN = window became hidden
* EXPOSED = window was exposed (redraw is required)
* MOVED = move was moved
* RESIZED = window was resized
* SIZE\_CHANGED = window size was changed
* MINIMIZED = window was minimized
* MAXIMIZED = window was maximized
* RESTORED = window was restored to normal size, position
* ENTER = gained mouse focus
* LEAVE = lost mouse focus
* FOCUS\_GAINED = gained keyboard focus
* FOCUS\_LOST = lost keyboard focus
* CLOSE = window is closed, although you may not see this happen

N.B. `ENTER`/`LEAVE` and `FOCUS_GAINED`/`FOCUS_LOST` may be staggered in GUI environments that opt away from click-to-focus (e.g. focus-follows-mouse or sloppy-focus).





# Building

Library dependencies are:
 * `libsdl2`
 * `libsdl2-ttf`

A makefile is provided, which generates `SDL_DumpEvents` in the base directory.

```
$ make
$ ./SDL_DumpEvents
```



# Running

In the Steam Client, add the binary as a Non-Steam Game.

* normal window mode:
  1. along the top menu bar, open "Games" menu.
  2. choose "Add a Non-Steam Game to My Library..."
  3. navigate to the binary using "BROWSE.."
  4. adjust File type dropdown at the bottom (to All Files)
  5. navigate to the compiled binary
  6. double-click the binary, or click the file then click "OPEN".
  7. as the browse window closes, then choose "ADD SELECTED PROGRAMS".
  8. the program should now appear in your Steam library listing.
  9. select the program in the Steam Library, then (click) PLAY, to run.
  10. press Escape (or click window close) to quit the program.
* Big Picture mode:
  cannot be done (yet)?

