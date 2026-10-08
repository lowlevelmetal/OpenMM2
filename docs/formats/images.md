# TGA, JPG and BMP images

Decoders: `asset::decodeTga` (own implementation) and `asset::decodeStb`
(stb_image, JPEG/BMP/PNG). `asset::decodeImageFile` picks one by extension.

* `texture/*.tga` (307) and `tune/*.tga` (4): uncompressed true-colour TGA,
  24-bit (214) or 32-bit (93), bottom-left origin, no ID field. They are
  UI/HUD art (race type icons, buttons, map dots). All decode. Nine of them
  (the map dots) are also used on the HUD map mesh `hudmap_square.pkg`.
* `jpg/*.jpg` (388): baseline JFIF, UI backgrounds and dialog art. All decode.
* No BMP or PNG files ship; both are supported for mods.

## MM2's readers

The decoders here accept more than MM2's (**verified** against the
decompile); every retail image decodes the same in both:

* `gfxLoadTargaImage` reads the 18-byte header and then raw pixels: 32-bit
  files become RGBA8888 images and anything else is read as 24-bit RGB. It
  ignores the ID field, the colour map, the image type (so no RLE) and the
  right-to-left bit; it honours the top-left origin bit.
* `gfxLoadBmpImage` reads `texture/<name>.bmp`: uncompressed 8-bit
  (paletted, opaque) or 24-bit only, and it rounds an odd width up by one
  and reads rows without their 4-byte padding.
* `gfxLoadJPEGImage` reads `jpg/<name>.jpg` with the IJG libjpeg (version
  6 API) into an RGB888 image.
* PNG is not supported by MM2.

A 32-bit TGA (and a PNG or BMP with alpha) sets `asset::Image::alphaFormat`,
the format-based alpha test MM2 applies (`gfxTexture::Create`).

## Row order

All decoders return rows **bottom-up**, picture bottom in row 0: TGA keeps
its native bottom-left order (rows are flipped only when the top-left origin
bit is set); JPEG/PNG/BMP, which stb returns top-down, are flipped. A UI quad
showing one of these images therefore maps v = 1 to the top of the screen
rectangle.

MM2 stores these images the other way round: its TGA, BMP and JPEG readers
all put the picture's top row first (only `.tex` files are copied in file
order). Its 2D drawing blits them upright, as OpenMM2's UI does. On a 3D mesh
MM2's v = 0 is the top of a TGA picture, while OpenMM2 uploads row 0 (the
bottom) as v = 0, so a TGA on a mesh is upside down relative to MM2 unless
the renderer flips it. The only retail case is the HUD map dots, which are
symmetric.
