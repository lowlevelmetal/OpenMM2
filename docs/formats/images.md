# TGA, JPG and BMP images

Decoders: `asset::decodeTga` (own implementation) and `asset::decodeStb`
(stb_image, JPEG/BMP/PNG). `asset::decodeImageFile` picks one by extension.

* `texture/*.tga` (307) and `tune/*.tga` (4): uncompressed true-colour TGA,
  24-bit (214) or 32-bit (93), bottom-left origin. They are UI/HUD art (race
  type icons, buttons, map dots). All decode.
* `jpg/*.jpg` (388): baseline JFIF, UI backgrounds and dialog art. All decode.
* No BMP or PNG files ship; both are supported for mods.

All decoders return rows **bottom-up** like `.tex` (see tex.md): TGA keeps its
native bottom-left order (rows are flipped only when the top-left origin bit
is set); JPEG/PNG/BMP, which stb returns top-down, are flipped. A UI quad
showing one of these images therefore maps v = 1 to the top of the screen
rectangle.
