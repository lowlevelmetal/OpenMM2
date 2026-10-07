# MTX — part pivots (`geometry/<model>_<part>.mtx`)

Parser: `src/asset/Mtx.{h,cpp}`. 819/819 retail files parse; every file is
48 bytes = 12 little-endian floats:

| Floats | Field |
|--------|-------|
| 0–2  | min of the part's box |
| 3–5  | max of the part's box |
| 6–8  | centre of the box |
| 9–11 | origin (pivot) |

`<part>` is the lower-case PKG part name (`whl0`, `headlight1`, `break01`,
`dash_speed_needle` for `<car>_dash.pkg`, …) or `(null)` for single-part city
objects. A few pivots have no mesh part (`exhaust0/1`, `trailer_hitch`).

## Semantics (verified against mesh bounds)

Part meshes in the PKG are stored around their own origin. `origin` is where
that origin sits in the model (car) space, or in world space for `(null)` city
objects (it then equals the PKG `offset` chunk). There is no rotation.

The box comes in two flavours, both present in the retail data:

* relative to the part (308 files): min/max equal the mesh bounds, centre ≈ 0
  (wheels, headlights);
* already offset by `origin` (379 files): min/max = mesh bounds + origin and
  centre == origin (city objects, some breakables).

Either way `max - min` is the part's size; `asset::Mtx::halfExtent()` uses it.
Seven files (`va_cooper_l_whl*`, `vp4x4_break03`, the century trailer's
`twhl0`) do not match their mesh bounds exactly; the meshes were edited after
the pivots were exported.

## Wheels

`whl0` front left, `whl1` front right, `whl2` rear left, `whl3` rear right,
`whl4`/`whl5` a third axle (fire truck, semi). Cars face −Z, so front wheels
have negative z and left wheels negative x. `asset::loadVehicleModel` derives
`radius = (max.y − min.y) / 2` and `width = max.x − min.x`; for `vpbug` this
gives radius 0.336 m at (±0.75, 0.307, −1.184 / 1.381). How the game itself
derives the physical wheel radius (from the mtx, the mesh or the tune file) is
not yet confirmed.
