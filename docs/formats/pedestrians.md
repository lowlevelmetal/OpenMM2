# Pedestrians (`anim/`)

Pedestrian skeletons, meshes, clothing variants and animations live under
`anim/` in `MM2CORE.AR`. Implemented by `src/asset/Ped.{h,cpp}`, inspected with
`mm2tool pedinfo|ped2obj|pedpose|pedcheck`.

**Verified** against build 3390: every file under `anim/` parses (88 files,
plus one stray file noted below). All four types load with consistent
skeleton, mesh, shader and animation data. Posed meshes rendered by
`mm2tool pedpose` show natural walk, run, dive and get-up motion.

Four types exist. Each consists of `pedmodel_<type>.{skel,mod,shaders,rays,csv}`
(plus `.remap` for `pedmodel_woman`):

| Type | Mesh material prefix | Layout | Variants |
|------|----------------------|--------|----------|
| `pedmodel_man` | `Businessman1` | packet | 48 |
| `pedmodel_manw` | `Winterman1` (long coat) | packet | 24 |
| `pedmodel_woman` | `bwoman1` | flat | 48 |
| `pedmodel_womanw` | winter woman | flat | 24 |

`pedmodel_wolf.skel` is a skeleton with no mesh or animations. The `w` types
share the animations of their base type. Which type and variant the game
spawns where is decided by the executable and is **not known**.
`anim/pedanim_manantrnch.anim` is a 104-byte text file with an `.anim` name.
It is broken as shipped and unreferenced (`isKnownBrokenPedAsset`).

## Skeleton (`.skel`, text)

```
NumBones 19
bone root {
    offset 0.000570 1.147210 -0.000000
    bone spine {
        offset 0.000000 0.050592 0.018812
        ...
```

The bones, depth-first: root, spine, neck, head, clavicle_r, shoulder_r,
elbow_r, wrist_r, clavicle_l, shoulder_l, elbow_l, wrist_l, pelvis, hip_l,
knee_l, ankle_l, hip_r, knee_r, ankle_r. That order is also the animation
channel order. `offset` is the bone origin in the parent's space. The rest
pose is a T-pose about 2 m tall with the feet at y = 0, facing -Z, with the
right arm along +X.

## Animation (`pedanim_*.anim`, binary, little-endian)

| Offset | Type | Meaning |
|-------:|------|---------|
| 0 | u32 | always 0 |
| 4 | u32 | frame count |
| 8 | u32 | channel count, always 60 |
| 12 | f32 | cycle distance (see below; **inferred**) |
| 16 | u8 | always 1, meaning **unknown** |
| 17 | f32[frames × channels] | channel data |

Each frame holds the root translation (3 floats, model space), then one Euler
vector per bone in skeleton order (19 × 3 floats, radians). That makes
60 = 3 + 19 × 3; **verified** by file sizes and by the rendered poses.

Posing follows MM1's `bnSkeleton::Pose` / `bnBone::Pose` / `bnBone::Transform`
(Open1560 `game.asm`). MM2's runtime classes are different (`crSkeleton`,
`crAnimation` per mm2hook), but the data fits this convention:

* Bone local rotation = `Matrix34::FromEulersXZY(euler)`. Ported directly as
  `matrixFromEulersXZY`. With row vectors this is `Rx · Rz · Ry`: rotate about
  X, then Z, then Y. The MM1 code skips sin/cos for exactly-zero angles.
* Bone local translation = skeleton offset. The root's translation comes from
  the animation and replaces its offset.
* `model = local · parentModel`.

MM2's use of XZY is **inferred** from three checks. Lying poses rule out
every order that starts with Y, because those orders put the body through the
floor. Of the six orders, XZY gives the most symmetric left/right arm swing in
the walk cycle. It also gives perfectly symmetric wrists in the idle pose.

The `cycle distance` header value matches `-(2·z[n-1] − z[n-2] − z[0])` for
most files: the forward travel of the root over n frame steps, i.e. one full
loop including the step back to frame 0. A few files differ by up to 0.15
(`manwalk`, `manrun`, `manw2bk`). This value is **inferred**.

The playback rate is **unknown**. The walk covers 1.41 m in 20 frames, which
gives a normal walking pace (≈1.4 m/s) at 20 fps. The original's
interpolation between frames is also unknown; `posePed` interpolates channels
linearly.

## Animation table (`pedmodel_*.csv`)

```
# anim name,mma name,first frame,last frame,Y AXIS Offset,Y AXIS DISTANCE,X AXIS Offset,X AXIS DISTANCE,default next
WALK,pedanim_manwalk,1,20,0.281,1.409,0,0,WALK
WALK_LDIVE,pedanim_manw2dl,1,24,0,0,0,2.224,LDIVE_GROUNDL
```

* States are named after the motion; transitions are named `FROM_TO`. The
  last column is the state that follows.
* Frames are 1-based and inclusive. The last frame is sometimes one past the
  `.anim` frame count (STAND2 1–30 vs 29 frames, RUN_WALK 1–10 vs 9), so clamp.
* "Y AXIS" is forward (-Z). "X AXIS" is sideways, positive to the left (-X):
  left dives move the root to -X. The distances are approximately the root
  travel: they match either the header's cycle distance or the first-to-last
  root travel within 0.01 for most moving states. They are hand-entered and
  some are off; for example, the dive side distances say 2.2 where the root
  moves 1.2–1.9. The offsets are close to the root's start position (WALK:
  0.281 = −z₀). Use the animation data for root motion; how the original
  consumed these columns is **unknown**.

## Mesh (`.mod`, text, "version: 1.09")

MM1 loaded the same family of files with `GetModel` (Open1560 `game.asm`
contains its keyword table; MM1 used version 1.06). The header gives pool
sizes:

```
version: 1.09
verts: 117   normals: 248   colors: 1   tex1s: 45   tex2s: 0   tangents: 0
materials: 18   adjuncts: 248   primitives: 230   matrices: 19
```

Then the pools: `v x y z`, `n x y z`, `c r g b a`, `t1 u v`. Then materials:

```
mtl Businessman1:SKIN {
    packets: 1          (packet layout)   or   adjuncts: 90 (flat layout)
    primitives: 30
    textures: 0
    illum: diffuse
    ambient: r g b
    diffuse: r g b
    specular: r g b
}
```

An *adjunct* is one rendered vertex:
`adj <vertex> <normal> <color> <tex1> <tex2> [matrix slot]`. Positions and
normals are in the space of the vertex's bone. Skinning is rigid: every vertex
belongs to exactly one bone, with no conflicting assignments in any retail
file. All primitives are triangles. No retail material is textured, so the
`t1` coordinates are unused.

**Packet layout** (`pedmodel_man`, `pedmodel_manw`): the packets follow all
materials and are owned in material order (`packets: N` each).

```
packet <adjuncts> <triangles> <matrices> {
    adj v n c t1 t2 slot     (slot indexes the packet's mtx list)
    tri a b c                (indices into this packet's adjuncts)
    mtx <bone> <bone> ...
}
```

A material split over several packets (Winterman1:JACKET has 4) repeats the
adjuncts its packets share. The header's `adjuncts` counts the unsplit pool,
which equals the normal count.

**Flat layout** (`pedmodel_woman`, `pedmodel_womanw`): one global list of
`adj v n c t1 t2` and of `tri a b c` (global adjunct indices). Materials
consume them in order (`adjuncts:` / `primitives:`). The file ends with
`mtxv <19 counts>` and `mtxn <19 counts>`. The vertex and normal pools are
sorted by bone, and these are the per-bone counts. Every adjunct's vertex and
normal fall in the same bone's range.

## Clothing variants (`.shaders`, binary)

```
u32 variantCount   (48 or 24)
u32 materialCount  (= .mod materials)
variantCount × materialCount × {
    u8   nameLength
    char name[nameLength]      texture name, empty in every retail file
    f32  diffuse[4], ambient[4], specular[4], emissive[4], power
}
```

The 17 floats are a Direct3D 7 `D3DMATERIAL7`. **Verified**: for every type,
one variant reproduces the `.mod`'s own diffuse, ambient and specular colours
exactly (man: variant 10, woman: variant 47), and entry *i* of a variant
colours material *i*.

## `.rays` (text, meaning unknown)

A bone count (19), then one row per bone of `f32 f32 f32 i32 i32` (the
integers are 0–18, so plausibly bone indices). After that comes one row of 19
integers (0–17) per shader variant. The name suggests collision rays, but
nothing confirms it. The file is parsed and exposed as `PedRays`.

## `.remap` (text, meaning unknown)

`pedmodel_woman.remap`: `17` then `1 3 2 5 4 7 6 9 8 11 10 13 12 15 14 17 16`,
a pairwise swap of 1-based indices. It is **not** a material remap: shader
entries match `.mod` materials without it. Parsed and exposed as
`PedType::remap`.
