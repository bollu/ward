<p align="center">
<img src="https://github.com/bollu/ward/raw/master/icon.png" alt="icon" style="float:right;" > 
<h1 align="center"> `WARD`: `WARD` 'aint really for drawing  </h1>
</p>

<img src="https://github.com/bollu/ward/raw/master/static/ward-drawing.png" alt="alt text" width="whatever" height="256">



No-nonsense infinite whiteboard, in the [suckless tradition](https://suckless.org/).

#### This lacks:

- multiple types of brushes.
- layers.
- save/load.
- custom palettes
- ... custom anything, really.

#### This features:

- Color palette of material colors.
- panning on an infinite whiteboard.
- quick minimap view to view the entire whiteboard at a glance.
- 530 lines of readable c++ code.

#### shortcuts:

- `Q`: undo
- `W`: redo
- `E`: toggle `E` raser
- `R`: `R` otate to next color.
- lower wacom button + drag: pan.
- upper wacom button: toggle overview. Tap to move to a location in the overview.
- Color selection: hover pointer over color in color palette.
- Eraser selection: hover pointer over eraser in color palette.

The code uses:

- the [EasyTab](https://github.com/ApoorvaJ/EasyTab) library to work with Wacom tablets.
- The [Milton project](https://github.com/serge-rgb/milton) as heavy reference on how to work with tablets.
- [SDL2](https://www.libsdl.org/) for rendering


