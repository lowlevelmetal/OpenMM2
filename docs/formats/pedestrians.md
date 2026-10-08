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
share the animations of their base type. MM2 (build 3393, `aiMap::Init`,
`aiPedestrian::Init`) picks each pedestrian's type uniformly from the AI
map's `[GoodWeatherPedName / BadWeatherPedName]` list (man and woman in both
cities), the second name of each pair (the `w` winter models) in snow, and
its variant as trunc(frand x (variants - 1)), so the last variant is never
used. See docs/ai.md.
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

MM2 reads it with `crSkeletonData::Load` / `crBoneData::Load` (**verified**):
`NumBones` and its count, then each `bone <name> {` with its `offset`
vector, optional `rotmin x y z` and `rotmax x y z` Euler limits (default
−π and π, used by `crBoneData::ApplyLimits`; no retail file has them), its
child bones and `}`. A file whose first bone keyword is not `bone` is "not a
valid skel file". Bone names are matched exactly (`crSkeletonData::FindBone`).

## Animation (`pedanim_*.anim`, binary, little-endian)

| Offset | Type | Meaning |
|-------:|------|---------|
| 0 | u32 | always 0 |
| 4 | u32 | frame count (1–10000) |
| 8 | u32 | channel count (1–1000), always 60 |
| 12 | f32 | cycle distance (see below; **inferred**) |
| 16 | u8 | always 1, meaning **unknown** |
| 17 | f32[frames × channels] | channel data |

`crAnimation::LoadAnim` (**verified**) also reads an older layout, used by no
retail file: when the first word is not 0 it is the frame count itself and
the word at offset 4 is a bone count (channels = bones × 3 + 3), with the
rest shifted up by four bytes. Out-of-range counts make the file "corrupted";
data after the last frame is ignored.

Each frame holds the root translation (3 floats, model space), then one Euler
vector per bone in skeleton order (19 × 3 floats, radians). That makes
60 = 3 + 19 × 3; **verified** by file sizes and by the rendered poses.

Posing is **verified** against MM2's `pedAnimationInstance::Draw`, which
poses the skeleton like `crAnimFrame::Pose` and then `crBoneData::Transform`:

* Bone local rotation = `Matrix34::FromEulersXZY(euler)`, ported as
  `matrixFromEulersXZY` with MM2's grouping of the products. With row vectors
  this is `Rx · Rz · Ry`: rotate about X, then Z, then Y. Exactly-zero angles
  skip sin/cos.
* Bone local translation = skeleton offset. The root's translation comes from
  the animation and replaces its offset.
* `model = local · parentModel`.

The `cycle distance` header value matches `-(2·z[n-1] − z[n-2] − z[0])` for
most files: the forward travel of the root over n frame steps, i.e. one full
loop including the step back to frame 0. A few files differ by up to 0.15
(`manwalk`, `manrun`, `manw2bk`). This value is **inferred**.

MM2 (`pedAnimation::Load`, `pedAnimationInstance`) plays the animations at
**30 frames per second**, in whole frames (no interpolation), from frame 0
of the `.anim` whatever the CSV's first frame says. At load it subtracts the
root's straight-line x/z drift between the first and the last frame from
every frame, so the pose stays in place and the pedestrian is moved by the
sequence's speed instead (below). `posePed` interpolates channels linearly;
the AI hands it whole frames.

## Animation table (`pedmodel_*.csv`)

```
# anim name,mma name,first frame,last frame,Y AXIS Offset,Y AXIS DISTANCE,X AXIS Offset,X AXIS DISTANCE,default next
WALK,pedanim_manwalk,1,20,0.281,1.409,0,0,WALK
WALK_LDIVE,pedanim_manw2dl,1,24,0,0,0,2.224,LDIVE_GROUNDL
```

* States are named after the motion; transitions are named `FROM_TO`. The
  last column is the state that follows; it may be missing. MM2
  (`pedAnimation::Load`) skips lines that start with `#`, splits the others
  with `strtok` (so an empty field between two commas disappears), reads
  numbers with `atoi`/`atof` and looks names up exactly
  (`pedAnimation::LookupSequence`); an unknown next state is fatal. A
  sequence whose last frame precedes its first is played backwards by MM2;
  OpenMM2 rejects such a table (none ships).
* Frames are 1-based and inclusive. The last frame is sometimes one past the
  `.anim` frame count (STAND2 1–30 vs 29 frames, RUN_WALK 1–10 vs 9), so clamp.
* "Y AXIS" is forward (-Z). "X AXIS" is sideways, positive to the left (-X):
  left dives move the root to -X. The distances are approximately the root
  travel: they match either the header's cycle distance or the first-to-last
  root travel within 0.01 for most moving states. They are hand-entered and
  some are off; for example, the dive side distances say 2.2 where the root
  moves 1.2–1.9. The offsets are close to the root's start position (WALK:
  0.281 = −z₀). MM2 moves a pedestrian by distance / (frames x 0.03333 s)
  per second forward and sideways, frames = last - first + 1 (WALK: man
  1.409 m over 20 frames = 2.11 m/s); the offsets are not used
  (`pedAnimation::Load`).

## Mesh (`.mod`, text, "version: 1.09")

MM2 reads it with `modGetModel` / `modModel::LoadAscii` (**verified**):
versions `1.08` and `1.09` are text, `2.00` is a binary form read with
`modModel::LoadBinary` (not supported by OpenMM2; no retail file uses it),
anything else fails. MM1 loaded the same family of files (version 1.06). The
header gives pool sizes, in this order:

```
version: 1.09
verts: 117   normals: 248   colors: 1   tex1s: 45   tex2s: 0   tangents: 0
materials: 18   adjuncts: 248   primitives: 230   matrices: 19
```

Then the pools: `v x y z`, `n x y z`, `c r g b a`, `t1 u v` (then `t2` and
tangent pools, empty in retail files). With fewer than two colours MM2 reads
the one `c` line but gives the vertices no colour, so they are white. Then
materials:

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

A material with `textures:` above 0 also has `texture: <n> <name>` lines;
the first names its texture. Alpha is always 1, emissive and power 0.
`LoadAscii` moves materials with a translucent texture to the end of the
list (for draw order); no retail material is textured, so this never
happens and OpenMM2 does not do it.

An *adjunct* is one rendered vertex:
`adj <vertex> <normal> <color> <tex1> <tex2> [matrix slot]`. Positions and
normals are in the space of the vertex's bone. Skinning is rigid: every vertex
belongs to exactly one bone, with no conflicting assignments in any retail
file. All primitives are triangles. No retail material is textured, so the
`t1` coordinates are unused.

**Packet layout** (`pedmodel_man`, `pedmodel_manw`): the packets follow all
materials and are owned in material order (`packets: N` each).

```
packet <adjuncts> <triangles> <matrices> [<reskins>] {
    adj v n c t1 t2 slot     (slot indexes the packet's mtx list; absent
                              when the packet has no matrices)
    reskin ...               (blend weights; none in retail files)
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
normal fall in the same bone's range. MM2 uses only `mtxv` (each vertex takes
the bone whose range holds it) and subtracts each material's first adjunct
from its triangle indices.

## Clothing variants (`.shaders`, binary)

```
u32 variantCount   (48 or 24; bit 0x80 = compact byte colours)
u32 materialCount  (= .mod materials)
variantCount × materialCount × {
    u8   nameLength
    char name[nameLength]      texture name, empty in every retail file
    f32  diffuse[4], ambient[4], specular[4], emissive[4], power
}
```

This is the shader table of a PKG `shaders` chunk: `pedAnimationInstance::Load`
reads it with `modShader::LoadShaderSet` (**verified**), so the colours are
rounded to 1/32 steps and the ambient colour is replaced by the diffuse one
(see pkg.md). Without a `.shaders` file the type has a single variant, the
`.mod`'s own materials. For every type, one variant reproduces the `.mod`'s
own diffuse colours (after that rounding; man: variant 10, woman: variant
47), and entry *i* of a variant colours material *i*.

## `.rays` (text)

A bone count (19), then one row per bone of `f32 f32 f32 i32 i32` (the
integers are 0–18, so plausibly bone indices). After that comes one row of 19
integers (0–17) per shader variant. `pedAnimationInstance::Load` reads it
(optional) into per-bone arrays (two floats, a third float and two bytes per
bone, and a byte per bone per variant); when the bone count differs from the
skeleton's it logs "Number of bones changed, can't load rays file" and leaves
them zero. What uses them is not traced yet. Parsed and exposed as
`PedRays`.

## `.remap` (text, meaning unknown)

`pedmodel_woman.remap`: `17` then `1 3 2 5 4 7 6 9 8 11 10 13 12 15 14 17 16`,
a pairwise swap of 1-based indices. It is **not** a material remap: shader
entries match `.mod` materials without it. MM2 never reads it (there is no
`remap` string in the executable). Parsed and exposed as `PedType::remap`
for tools.
