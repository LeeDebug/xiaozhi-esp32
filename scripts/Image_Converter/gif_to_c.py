#!/usr/bin/env python3
"""
GIF -> LVGL .c converter.

Converts an animated GIF into a single C file containing one
``lv_image_dsc_t`` per frame plus a frame table for easy animation:

    const lv_image_dsc_t *waves_frames[];
    const uint16_t waves_durations_ms[];
    const uint16_t waves_frame_count;
    const uint16_t waves_width / waves_height;

Usage:
    pip install Pillow pypng lz4
    python gif_to_c.py waves.gif -o ./output --cf RGB565
    python gif_to_c.py waves.gif -o ./output --cf RGB565A8 --resize 320x240
    python gif_to_c.py waves.gif -o ./output --cf ARGB8888 --compress LZ4 --fps 20

Playback on ESP32 (LVGL9):
    extern const lv_image_dsc_t *waves_frames[];
    extern const uint16_t waves_durations_ms[];
    extern const uint16_t waves_frame_count;
    static uint16_t idx = 0;
    lv_image_set_src(img_obj, waves_frames[idx]);
    idx = (idx + 1) % waves_frame_count;
"""
import argparse
import re
import tempfile
from os import path
from pathlib import Path

from LVGLImage import (
    LVGLImage,
    ColorFormat,
    CompressMethod,
    LVGLCompressData,
)

try:
    from PIL import Image
except ImportError as e:
    raise ImportError("Need Pillow package, do `pip install Pillow`") from e


def sanitize_c_name(name: str) -> str:
    name = re.sub(r"[^0-9a-zA-Z_]", "_", name)
    if name and name[0].isdigit():
        name = "_" + name
    return name or "gif_image"


def gif_frames(gif_path, resize=None, keep_aspect=True):
    """Yield (rgba_image, duration_ms) for each frame."""
    img = Image.open(gif_path)
    n = getattr(img, "n_frames", 1)
    for i in range(n):
        img.seek(i)
        frame = img.convert("RGBA")
        duration = int(img.info.get("duration", 100) or 100)
        if resize:
            tw, th = resize
            if keep_aspect:
                frame.thumbnail((tw, th), Image.LANCZOS)
                canvas = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
                canvas.alpha_composite(frame, ((tw - frame.width) // 2,
                                              (th - frame.height) // 2))
                # flatten transparent canvas on black for non-alpha formats later
                frame = canvas
            else:
                frame = frame.resize((tw, th), Image.LANCZOS)
        yield frame, duration


def rgba_to_lvgl_image(frame: Image.Image, cf: ColorFormat,
                       background=0x000000, dither=False,
                       align=1, premultiply=False) -> LVGLImage:
    # Save frame as temp PNG so we can reuse LVGLImage.from_png()
    with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as tmp:
        tmp_path = tmp.name
    frame.save(tmp_path, "PNG")
    try:
        lv = LVGLImage().from_png(tmp_path, cf,
                                  background=background,
                                  rgb565_dither=dither)
        lv.adjust_stride(align=align)
        if premultiply:
            lv.premultiply()
        return lv
    finally:
        try:
            Path(tmp_path).unlink()
        except OSError:
            pass


def write_gif_c_file(varname: str, frames, durations, out_path: str,
                     compress: CompressMethod):
    """frames: list of LVGLImage (already stride-adjusted)."""
    macro = "LV_ATTRIBUTE_" + varname.upper()
    flags = "0"
    if compress is not CompressMethod.NONE:
        flags += " | LV_IMAGE_FLAGS_COMPRESSED"
    premult = any(f.premultiplied for f in frames)
    if premult:
        flags += " | LV_IMAGE_FLAGS_PREMULTIPLIED"

    def c_bytes(data: bytes, per_line=16) -> str:
        lines = []
        for i in range(0, len(data), per_line):
            chunk = data[i:i + per_line]
            lines.append("    " + ",".join(f"0x{v:02x}" for v in chunk) + ",")
        return "\n".join(lines)

    with open(out_path, "w+") as f:
        f.write('#if defined(LV_LVGL_H_INCLUDE_SIMPLE)\n#include "lvgl.h"\n'
                '#elif defined(LV_BUILD_TEST)\n#include "../lvgl.h"\n#else\n'
                '#include "lvgl/lvgl.h"\n#endif\n\n\n'
                '#ifndef LV_ATTRIBUTE_MEM_ALIGN\n#define LV_ATTRIBUTE_MEM_ALIGN\n#endif\n\n'
                f'#ifndef {macro}\n#define {macro}\n#endif\n\n')
        for i, lv in enumerate(frames):
            data = (LVGLCompressData(lv.cf, compress, lv.data).compressed
                    if compress != CompressMethod.NONE else bytes(lv.data))
            f.write(f"static const\nLV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST {macro}\n"
                    f"uint8_t {varname}_frame{i}_map[] = {{\n{c_bytes(data)}\n}};\n\n")
            f.write(f"static const lv_image_dsc_t {varname}_frame{i} = {{\n"
                    f"  .header.magic = LV_IMAGE_HEADER_MAGIC,\n"
                    f"  .header.cf = LV_COLOR_FORMAT_{lv.cf.name},\n"
                    f"  .header.flags = {flags},\n"
                    f"  .header.w = {lv.w},\n"
                    f"  .header.h = {lv.h},\n"
                    f"  .header.stride = {lv.stride},\n"
                    f"  .data_size = sizeof({varname}_frame{i}_map),\n"
                    f"  .data = {varname}_frame{i}_map,\n}};\n\n")
        f.write(f"const lv_image_dsc_t *{varname}_frames[] = {{\n")
        for i in range(len(frames)):
            f.write(f"  &{varname}_frame{i},\n")
        f.write("};\n\n")
        f.write(f"const uint16_t {varname}_durations_ms[] = {{\n")
        for d in durations:
            f.write(f"  {int(d)},\n")
        f.write("};\n\n")
        w = frames[0].w if frames else 0
        h = frames[0].h if frames else 0
        f.write(f"const uint16_t {varname}_frame_count = {len(frames)};\n"
                f"const uint16_t {varname}_width = {w};\n"
                f"const uint16_t {varname}_height = {h};\n")


def parse_resize(s: str):
    if not s:
        return None
    m = re.match(r"(\d+)[xX,:\s]+(\d+)", s)
    if not m:
        raise ValueError(f"invalid --resize '{s}', e.g. 320x240")
    return int(m.group(1)), int(m.group(2))


def main():
    ap = argparse.ArgumentParser(description="GIF to LVGL .c converter")
    ap.add_argument("input", help="input .gif file or folder of .gif files")
    ap.add_argument("-o", "--output", default="./output_gif",
                    help="output folder, default ./output_gif")
    ap.add_argument("--cf", default="RGB565",
                    choices=["RGB565", "RGB565A8", "ARGB8888", "XRGB8888",
                             "RGB888", "A8", "I8", "AUTO"],
                    help="LVGL color format")
    ap.add_argument("--compress", default="NONE", choices=["NONE", "RLE", "LZ4"])
    ap.add_argument("--align", default=1, type=int)
    ap.add_argument("--background", default=0x000000,
                    type=lambda x: int(x, 0))
    ap.add_argument("--dither", action="store_true",
                    help="RGB565 dithering for gradients")
    ap.add_argument("--premultiply", action="store_true")
    ap.add_argument("--resize", default=None,
                    help="e.g. 320x240, thumbnail-fit into this box")
    ap.add_argument("--stretch", action="store_true",
                    help="stretch to --resize instead of aspect-fit")
    ap.add_argument("--fps", default=None, type=float,
                    help="override frame rate, e.g. 20")
    ap.add_argument("--max-frames", default=0, type=int,
                    help="only take first N frames, 0 = all")
    ap.add_argument("--name", default=None,
                    help="C variable basename (default: gif filename)")
    args = ap.parse_args()

    if path.isfile(args.input):
        gif_files = [args.input]
    elif path.isdir(args.input):
        gif_files = sorted(Path(args.input).rglob("*.gif"))
    else:
        raise SystemExit(f"invalid input: {args.input}")
    if not gif_files:
        raise SystemExit("no .gif files found")

    cf = None if args.cf == "AUTO" else ColorFormat[args.cf]
    if cf is None:
        cf = ColorFormat.RGB565
    compress = CompressMethod[args.compress]
    resize = parse_resize(args.resize)
    Path(args.output).mkdir(parents=True, exist_ok=True)

    for gif in gif_files:
        gif = str(gif)
        varname = sanitize_c_name(args.name or Path(gif).stem)
        lv_frames, durations = [], []
        for frame, dur in gif_frames(gif, resize,
                                     keep_aspect=not args.stretch):
            if args.max_frames and len(lv_frames) >= args.max_frames:
                break
            lv = rgba_to_lvgl_image(frame, cf, args.background,
                                    args.dither, args.align, args.premultiply)
            lv_frames.append(lv)
            durations.append(dur)
        if args.fps and args.fps > 0:
            durations = [int(round(1000.0 / args.fps))] * len(durations)
        if not lv_frames:
            print(f"skip {gif}: no frames")
            continue
        out = str(Path(args.output) / f"{varname}.c")
        write_gif_c_file(varname, lv_frames, durations, out, compress)
        total = sum(len(bytes(f.data)) for f in lv_frames)
        print(f"done {gif} -> {out} "
              f"[{len(lv_frames)} frames {lv_frames[0].w}x{lv_frames[0].h} "
              f"{cf.name} ~{total // 1024}KB]")


if __name__ == "__main__":
    main()
