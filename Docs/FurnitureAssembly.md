# What we have done

Unreal Engine 5 project. The work that matters is turning a floorplan into a furnished house. Open the tool from **Window → Floorplan → Furniture Placement**.

Dates below are the days the work landed in this repository, or the day it is still sitting uncommitted.

## 23 September 2026 — the project

The first commit is the Unreal Engine 5 template: third person, combat, platforming, and side-scrolling variants, plus the mannequin (180 cm). It also includes `Content/FloorPlan.umap` and the wall mesh `Content/HouseMesh/floorplan-walls-feet`.

Before that commit, the plan was read outside the editor: a wall detector wrote wall segments, a furniture detector wrote object boxes, and feet on the sheet were converted to Unreal centimeters (1 ft = 30.48 cm). Those scripts are not in this repository. The C++ tool replaced that path.

## 29 September 2026 — place a detection list

**Tools path:** `Window → Floorplan → Furniture Placement`.

A detection JSON is a list of objects (a top-level array, or an `objects` array with optional root `units`). Each object has a category, a position, a rotation, and a scale. Import reads that list. It does not invent furniture.

**Coordinates.** Positions are in plan space from the **northwest corner** of the plan: **+X east**, **+Y south**, **+Z up**, in **centimeters** unless `units` is `ft` or `m`. Compute Assembly uses the same axes. Import converts units only; it does **not** flip or mirror axes. If furniture sits on the wrong side of the house, fix the JSON (or the floorplan mesh pivot), not the importer. Legacy files that were tuned while import negated Y should have each `position[1]` negated once in the file, then saved.

The window keeps one mesh per category on `Content/Furniture/DA_FurnitureCatalog`. Assign a mesh once and every object in that category uses it. **Place** spawns an `AFurniturePlacementActor` for each object.

The 1 BHK house came in with this step: the house meshes under `Content/HouseMesh/BHK_1`, the plan image `Content/Furniture/Floorplans/1BHK.png`, and the detection files `1BHK.json`, `1BHK_1.json`, and `1BHK_2.json`. A later commit the same day refreshed those meshes and detection files.

`UFloorplanDetector` is the hook for a future image detector. `USampleBHKFloorplanDetector` only returns the bundled 25 ft by 30 ft sample. The window does not call it. Import is JSON.

The branch was merged so a pull request could be opened.

## 5 October 2026 — solve a floorplan that has no furniture

**Compute Assembly** runs when the file has rooms and halls and no object list. `1BHK_floorplan.json` is that file. Import does three things:

1. Measure each room and write `wallDistance`, `swingRadius`, and `totalSize` back into the floorplan.
2. Place furniture and write `<plan>.computed.json` beside it.
3. Load that list so **Place** uses the same category meshes as a hand-made detection file.

The window label shows **Place Assembly** or **Compute Assembly**. An **Order of placement** list shows every mesh in the order **Place** spawns it, with location, rotation, scale, and mesh name.

### Floorplan file

Root fields: `units` (`cm`, `m`, or `ft`), optional `origin`, and `rooms`, `halls`, or `spaces`.

Each room has `name`, `origin`, `size`, and optional `doors` and `windows`. An opening has `wall` (`north`, `east`, `south`, `west`), `offset` along that wall, `width`, and `swing` (doors) or `depth` (windows). A missing swing uses the opening width.

Known rooms: bedroom, toilet (also bathroom, restroom, wc), utility, kitchen, dining, living (also lounge), parking. Hall, hallway, corridor, passage, lobby, and foyer are halls. Halls are counted and left empty. An unknown name is skipped.

### Room size and the grid

Wall distance is the room `size`. Swing radius is the door swing on the facing walls. `totalSize` is wall distance minus that swing, and is never smaller than a quarter of the wall span. Piece size follows `totalSize` when it is present.

Each room is a 10 cm grid. Border cells are walls. Door swing blocks cells. Windows are recorded but do not block the floor grid, because a window is above the floor. A placed piece occupies its cells and leaves a one-cell gap. If it does not fit, a second pass lets it use that gap, and the status line says clearance was reduced.

Height is a real-world height for that kind, compared with the 180 cm mannequin. The mesh is scaled uniformly so it does not flatten, then clamped so it stays inside the floor footprint and does not grow past about 1.15 times the mannequin.

Yaw is the direction the front faces: 0 east, 90 north, -90 south, 180 west.

### What each room got on 5 October

| Room | Pieces |
| --- | --- |
| Bedroom | Bed on a wall, bed lamp beside it, wardrobe beside that |
| Toilet | Toilet on a wall, wash basin beside it |
| Utility | Utility sink on a wall |
| Kitchen | Stove on a wall, kitchen sink beside it, fridge beside that |
| Dining | Dining table in the center, chairs around it |
| Living | Sofa on a wall, coffee table in front, television opposite, plant in a corner |
| Parking | Car along the long axis |

The first piece is the primary. Later pieces are skipped when the primary does not fit.

## 6 October 2026 — still uncommitted

This is in the working tree and is not in a commit yet.

**Living room television unit.** The catalog television mesh is measured before placement and enlarged to 1.8 times that mesh. The bottom of the set is 50 cm above the floor. A window or door that crosses the screen height rejects that wall, and the unit moves. The TV stand is scaled so its top meets the set. Speakers sit at the sides. A DVD player and a game console share that wall and rotation. New categories: TV Stand, Speaker, DVD Player, Game Console.

**Wall-mounted sinks.** Wash basin, utility sink, and kitchen sink hang on the wall. Their mesh bottoms are 55 cm, 55 cm, and 65 cm above the floor. The floor cells under them stay free for a cabinet. The basin and sink meshes are shorter (30 cm, 35 cm, 25 cm) because the mount height is separate from the mesh height.

**Dining set.** The table and chairs are solved together instead of as a table plus a separate chair pass.

**Content.** A sofa mesh is under `Content/Mesh/sofa/`. A static mesh of the 1 BHK floorplan is under `Content/HouseMesh/NewFolder1/1BHK_floorplan`. The catalog and `1BHK_floorplan.computed.json` were regenerated. `FloorPlan.umap` and `Floorplan3.umap` changed in the editor.

### What each room gets today

| Room | Pieces |
| --- | --- |
| Bedroom | Bed, bed lamp, wardrobe |
| Toilet | Toilet, wash basin mounted on the wall |
| Utility | Utility sink mounted on the wall |
| Kitchen | Stove, kitchen sink mounted on the wall, fridge |
| Dining | Table and chairs as one set |
| Living | Sofa, coffee table, television with stand, speakers, DVD player, and game console, plant |
| Parking | Car along the long axis |

## Source

| File | Role |
| --- | --- |
| `Source/AssemblyEditor/FloorplanFurnitureWindow.cpp` | Import, catalog meshes, order list, place |
| `Source/AssemblyEditor/AssemblyEditor.cpp` | Window menu entry |
| `Source/Assembly/Furniture/FurnitureJsonImport.cpp` | Reads an object list |
| `Source/Assembly/Furniture/FurnitureComputeAssembly.cpp` | Measures rooms and solves furniture |
| `Source/Assembly/Furniture/FurnitureTypes.h` | Categories, heights, mount heights |
| `Source/Assembly/Furniture/FurniturePlacementActor.cpp` | Spawns and scales each mesh |
| `Source/Assembly/Furniture/FurnitureCatalog.cpp` | One mesh per category |
| `Source/Assembly/Furniture/FloorplanDetector.cpp` | Unused image-detector hook and 1 BHK sample |
| `Source/Assembly/Furniture/FurnitureAssemblyLoad.cpp` | Shared import: Place Assembly or Compute Assembly, then spawn |
| `Source/AssemblyEditor/PlaceAssemblyCommandlet.cpp` | Opens a chosen level, Compute Assembly furniture only, first-person game mode |
| `Scripts/PlaceAssembly.bat` | File picker, then package that level as a Windows EXE |

## Command

The editor window still imports and places into the open level. The same JSON can also build a packaged game:

```
Scripts\PlaceAssembly.bat
```

The terminal lists every level under `Content` and asks which one the packaged exe should open. A file dialog asks for a floorplan JSON of rooms and halls. Compute Assembly solves it, places the objects with the catalog meshes into that level, and uses the first-person game mode. No floorplan mesh is spawned. The Windows executable is written under `Packaged`.
