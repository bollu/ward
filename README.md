# ward: ward 'aint ready for drawing

![](./static/ward-drawing.png | width=256)
![](./static/ward-overview.png | width=256)



No-nonsense infinite whiteboard, in the [suckless tradition](https://suckless.org/).

This lacks:
- multiple types of brushes.
- layers.
- save/load.
- custom palettes
- ... custom anything, really.

This features:

- An entirely pen-based workflow, no keyboard.
- Color palette of material colors.
- panning on an infinite whiteboard.
- quick minimap view to view the entire whiteboard at a glance.

The code uses:

- the [EasyTab](https://github.com/ApoorvaJ/EasyTab) library to work with Wacom tablets.
- The [Milton project](https://github.com/serge-rgb/milton) as heavy reference on how to work with tablets.
- [SDL2](https://www.libsdl.org/) for rendering

