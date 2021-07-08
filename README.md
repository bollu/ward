<p align="center">
<img src="https://github.com/bollu/ward/raw/master/icon.png" alt="icon" style="float:right;" > 
<h1 align="center"> <code>WARD</code>: <code>WARD</code> 'Aint Really for Drawing  </h1>
<img src="https://github.com/bollu/ward/raw/master/static/ward-drawing.png" alt="alt  height="256">
</p>




No-nonsense infinite whiteboard, in the [suckless tradition](https://suckless.org/).

#### This lacks:

- multiple types of brushes.
- layers.
- save/load.
- custom palettes
- ... custom anything, really.

#### This features:

- Rock solid framerate.
- Infinite undo/redo.
- panning on an infinite whiteboard.
- quick minimap view to view the entire whiteboard at a glance.
- Not written using web tech: 530 lines of readable c++ code.

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
- [SDL2](https://www.libsdl.org/) for rendering.
- [Spatial hashing](http://www.cs.ucf.edu/~jmesit/publications/scsc%202005.pdf) to quickly add and delete brush strokes.
- Lazy repainting of the entire frame upon change..

