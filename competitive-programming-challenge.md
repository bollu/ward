# WARD competitive programming question

Given online queries of the following form:
- `add_disk(dx, dy, dr)`: add a new disk centered at `(dx, dy)` with radius `dr`.
- `erase(ex, ey, er)`: Create a new "erasing disk" at `(ex, ey)` with radius `er`, and erase all previously added disks whose centers lie within the erasing disk.
   Said differently, erase all previous added disks `Disk(dx, dy, _)` such that the distance between `(dx, dy)` and `(ex, ey)` is less than `er`.
- `enumerate(aabbx, aabby, aabbw, aabbh)`: enumerate all disks whose centers lie within the axis aligned bounding box/rectangle whose bottom-left corner is `(aabbx, aabby)`,
   whose width and height is `aabbw`, `aabbh`.
- `start_undo_range`: start an undo range.
- `end_undo_range`: end the previously started undo range. Guaranteed that this will have a prior undo range to match with.
- `undo`: undo the commands performed in the undo range.
- `redo`: redo the last undo.

