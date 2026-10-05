# Furniture assembly

Unreal editor tool that turns a JSON file into placed furniture meshes. Open it from **Tools → Furniture Placement**.

There are two import paths. The window label shows which one ran.

## Place Assembly

This path is for a detection file that already lists objects. Each object needs a category, a position, a rotation, and a scale. Import reads that list and does not invent furniture.

The 1 BHK detection files under `Content/Furniture/Detections/` are this kind of file. The house meshes for that plan are already in the project.

## Compute Assembly

This path runs when the file has no object list and instead describes rooms and halls. It builds the same object records Place Assembly already imports, then places them with the same category meshes.

On import the window does three things:

1. Measure each room and write `wallDistance`, `swingRadius`, and `totalSize` back into the floorplan file.
2. Solve furniture into that room and write a sibling file named `<plan>.computed.json`.
3. Load that computed list into the window so **Place** can spawn the meshes.

A sample floorplan is `Content/Furniture/Detections/1BHK_floorplan.json`. Its solved output is `1BHK_floorplan.computed.json`.

### Floorplan file

The root is an object with `units` (`cm`, `m`, or `ft`), an optional `origin`, and arrays named `rooms`, `halls`, or `spaces`.

Each room has a `name`, an `origin`, a `size`, and optional `doors` and `windows`. A door or window names a `wall` (`north`, `east`, `south`, `west`), an `offset` along that wall, a `width`, and a `swing` (doors) or `depth` (windows). Missing swing uses the opening width.

Room names map to a known room: bedroom, toilet (also bathroom, restroom, wc), utility, kitchen, dining, living (also lounge), and parking. Hall, hallway, corridor, passage, lobby, and foyer are halls. Halls are counted and kept clear of furniture. An unknown room name is skipped.

### Room size

For each room, wall distance is the span between opposite walls, taken from `size`. Swing radius is the door swing on those walls, added when doors face each other across the room. `totalSize` is wall distance minus that swing, and is never smaller than a quarter of the wall span. Furniture sizes follow `totalSize` when it is present.

### What each room gets

| Room | Pieces |
| --- | --- |
| Bedroom | Bed on a wall, bed lamp beside it, wardrobe beside that |
| Toilet | Toilet on a wall, wash basin beside it |
| Utility | Utility sink on a wall |
| Kitchen | Stove on a wall, kitchen sink beside it, fridge beside that |
| Dining | Dining table in the center, chairs around it |
| Living | Sofa on a wall, coffee table in front, television on the opposite wall, plant in a corner |
| Parking | Car along the long axis of the room |

The first piece is the primary. Later pieces are skipped when the primary does not fit. Dining chairs are the exception: several can be emitted around the table.

### How a piece is placed

Each room is a 10 cm grid. The border cells are walls. Door swing and window depth mark cells that furniture cannot use. A placed piece occupies its cells and leaves a one-cell clearance around it. If a piece does not fit, a second pass allows it to use clearance cells and the status line says clearance was reduced.

Piece size is a share of the clear wall-to-wall distance, then clamped so it stays on the open run of that wall. The front of a wall piece faces into the room: yaw 0 is east, 90 is north, -90 is south, 180 is west.

Each exported object uses the catalog category name (`Bed`, `Sofa`, `Dining Chair`, and so on), so it keeps that category’s mesh. Position is the footprint center in centimeters. `source` is `compute`.

## Meshes

Assign one premodel mesh to each category in the window. Every object in that category uses that mesh. The assignment is stored on the furniture catalog asset.

## Source

| File | Role |
| --- | --- |
| `Source/AssemblyEditor/FloorplanFurnitureWindow.cpp` | Import, catalog meshes, and place |
| `Source/Assembly/Furniture/FurnitureJsonImport.cpp` | Reads an object list |
| `Source/Assembly/Furniture/FurnitureComputeAssembly.cpp` | Measures rooms and solves furniture |
| `Source/Assembly/Furniture/FurnitureTypes.h` | Room and furniture categories |
| `Source/Assembly/Furniture/FurniturePlacementActor.cpp` | Spawns the meshes in the level |
| `Source/Assembly/Furniture/FurnitureCatalog.cpp` | One mesh per category |
