#!/bin/bash
# run_pixel_tests.sh -- Run host graphics regressions with framebuffer assertions

set -e
cd "$(dirname "$0")"

BINARY=${BINARY:-./build/mmbasic_test}
if [ ! -x "$BINARY" ]; then
    echo "Binary not found. Building..."
    ./build.sh
fi

run_case() {
    local label="$1"
    local testfile="$2"
    shift 2

    echo "Running pixel test: $label"
    "$BINARY" "$testfile" --interp "$@"
    "$BINARY" "$testfile" --vm "$@"
    echo ""
}

run_vm_case() {
    local label="$1"
    local testfile="$2"
    shift 2

    echo "Running VM pixel test: $label"
    "$BINARY" "$testfile" --vm "$@"
    echo ""
}

run_case \
    "t60_fastgfx_pixels" \
    "tests/t60_fastgfx_pixels.bas" \
    --assert-pixel 12,12,000000 \
    --assert-pixel 32,12,00FF00 \
    --assert-pixel 15,30,000000 \
    --assert-pixel 40,30,FF0000 \
    --assert-pixel 11,50,000000

run_vm_case \
    "t60_fastgfx_text_font1" \
    "tests/t60_fastgfx_pixels.bas" \
    --assert-pixel 31,50,000000 \
    --assert-pixel 32,51,FFFFFF

run_case \
    "t61_blocks_screen_pixels" \
    "tests/t61_blocks_screen_pixels.bas" \
    --assert-pixel 0,0,000000 \
    --assert-pixel 99,78,FF0000 \
    --assert-pixel 138,94,FFFF00 \
    --assert-pixel 160,191,FF0000 \
    --assert-pixel 135,223,00FF00

run_vm_case \
    "t61_blocks_text_font1" \
    "tests/t61_blocks_screen_pixels.bas" \
    --assert-pixel 6,3,000000 \
    --assert-pixel 7,4,FFFFFF

run_case \
    "t85_box_optional_args" \
    "tests/t85_box_optional_args.bas" \
    --assert-pixel 12,12,FF0000 \
    --assert-pixel 34,14,000000 \
    --assert-pixel 30,10,00FF00

run_case \
    "t86_box_scalar_forms" \
    "tests/t86_box_scalar_forms.bas" \
    --assert-pixel 5,5,FFFFFF \
    --assert-pixel 7,7,000000 \
    --assert-pixel 17,5,FFFFFF \
    --assert-pixel 19,7,000000 \
    --assert-pixel 21,9,FFFFFF \
    --assert-pixel 29,5,FF0000 \
    --assert-pixel 41,5,00FF00 \
    --assert-pixel 55,7,0000FF \
    --assert-pixel 65,5,FFFFFF \
    --assert-pixel 67,7,FF0000 \
    --assert-pixel 77,5,FFFFFF \
    --assert-pixel 79,7,FFFF00 \
    --assert-pixel 89,5,00FFFF \
    --assert-pixel 91,7,FF00FF \
    --assert-pixel 96,6,FFFF00 \
    --assert-pixel 113,5,000000

run_case \
    "t109_box_array_forms" \
    "tests/t109_box_array_forms.bas" \
    --assert-pixel 10,10,FF0000 \
    --assert-pixel 12,12,FF0000 \
    --assert-pixel 24,10,00FF00 \
    --assert-pixel 26,12,000000 \
    --assert-pixel 38,10,0000FF \
    --assert-pixel 40,12,0000FF

run_case \
    "t88_circle_scalar_forms" \
    "tests/t88_circle_scalar_forms.bas" \
    --assert-pixel 20,16,FFFFFF \
    --assert-pixel 20,20,000000 \
    --assert-pixel 40,20,FF0000 \
    --assert-pixel 60,20,0000FF \
    --assert-pixel 60,16,00FF00 \
    --assert-pixel 80,20,FFFF00 \
    --assert-pixel 80,24,FFFF00

run_case \
    "t89_circle_array_forms" \
    "tests/t89_circle_array_forms.bas" \
    --assert-pixel 20,50,FF0000 \
    --assert-pixel 40,50,0000FF \
    --assert-pixel 40,46,0000FF \
    --assert-pixel 60,50,FFFF00 \
    --assert-pixel 60,55,FFFF00

run_case \
    "t110_circle_vector_radius_quirk" \
    "tests/t110_circle_vector_radius_quirk.bas" \
    --assert-pixel 20,15,FF0000 \
    --assert-pixel 40,15,0000FF \
    --assert-pixel 40,16,0000FF \
    --assert-pixel 60,15,00FF00 \
    --assert-pixel 60,16,00FF00

run_case \
    "t90_line_scalar_forms" \
    "tests/t90_line_scalar_forms.bas" \
    --assert-pixel 15,10,FF0000 \
    --assert-pixel 31,15,00FF00 \
    --assert-pixel 50,15,0000FF \
    --assert-pixel 35,5,FFFFFF \
    --assert-pixel 85,10,000000

run_case \
    "t111_line_cursor_defaults" \
    "tests/t111_line_cursor_defaults.bas" \
    --assert-pixel 15,12,FFFFFF \
    --assert-pixel 20,12,FFFFFF \
    --assert-pixel 35,12,000000

run_case \
    "t91_line_array_forms" \
    "tests/t91_line_array_forms.bas" \
    --assert-pixel 15,40,FF0000 \
    --assert-pixel 31,45,00FF00 \
    --assert-pixel 50,45,0000FF

run_case \
    "t92_text_scalar_forms" \
    "tests/t92_text_scalar_forms.bas" \
    --assert-pixel 10,10,000000 \
    --assert-pixel 11,11,FFFFFF \
    --assert-pixel 16,19,FFFFFF \
    --assert-pixel 58,21,00FF00 \
    --assert-pixel 56,20,000000 \
    --assert-pixel 103,21,0000FF \
    --assert-pixel 110,20,000000

run_case \
    "t93_text_string_args" \
    "tests/t93_text_string_args.bas" \
    --assert-pixel 124,40,000000 \
    --assert-pixel 126,41,FFFF00 \
    --assert-pixel 130,45,FFFF00 \
    --assert-pixel 135,41,000000

run_case \
    "t117_text_menu_glyphs" \
    "tests/t117_text_menu_glyphs.bas" \
    --assert-pixel 6,2,000000 \
    --assert-pixel 7,3,FFFFFF \
    --assert-pixel 16,3,FFFFFF \
    --assert-pixel 24,6,FFFFFF \
    --assert-pixel 40,6,FFFFFF

run_case \
    "t118_text_all_printable_font1" \
    "tests/t118_text_all_printable_font1.bas" \
    --assert-pixel 15,1,FFFFFF \
    --assert-pixel 38,1,FFFFFF \
    --assert-pixel 110,1,FFFFFF \
    --assert-pixel 186,1,FFFFFF \
    --assert-pixel 15,29,FFFFFF \
    --assert-pixel 121,43,FFFFFF \
    --assert-pixel 14,60,FFFFFF \
    --assert-pixel 157,60,FFFFFF \
    --assert-pixel 170,71,FFFFFF

run_case \
    "t94_cls_explicit" \
    "tests/t94_cls_explicit.bas" \
    --assert-pixel 0,0,0000FF \
    --assert-pixel 3,3,0000FF

run_case \
    "t95_cls_default" \
    "tests/t95_cls_default.bas" \
    --assert-pixel 0,0,000000 \
    --assert-pixel 3,3,000000

run_case \
    "t96_pixel_scalar_forms" \
    "tests/t96_pixel_scalar_forms.bas" \
    --assert-pixel 5,5,FFFFFF \
    --assert-pixel 8,8,00FF00 \
    --assert-pixel 6,8,000000

run_case \
    "t97_pixel_array_forms" \
    "tests/t97_pixel_array_forms.bas" \
    --assert-pixel 20,15,FF0000 \
    --assert-pixel 22,15,00FF00 \
    --assert-pixel 24,15,0000FF

run_case \
    "t101_pixel_rgb_dynamic" \
    "tests/t101_pixel_rgb_dynamic.bas" \
    --assert-pixel 20,20,FF0000 \
    --assert-pixel 22,20,00FF00 \
    --assert-pixel 24,20,0000FF \
    --assert-pixel 26,20,FFFF00

run_case \
    "t102_hslrgb_palette_pixels" \
    "tests/t102_hslrgb_palette_pixels.bas" \
    --assert-pixel 20,20,F20D0D \
    --assert-pixel 22,20,F2F20D \
    --assert-pixel 24,20,0DF20D \
    --assert-pixel 26,20,0D0DF2 \
    --assert-pixel 28,20,000000

run_case \
    "t103_mandel_viewport_min" \
    "tests/t103_mandel_viewport_min.bas" \
    --assert-pixel 0,0,F20D0D \
    --assert-pixel 10,0,F2630D \
    --assert-pixel 0,6,F20D0D \
    --assert-pixel 10,6,F2B90D \
    --assert-pixel 20,10,000000 \
    --assert-pixel 31,23,F2630D

run_case \
    "t104_for_local_integer_unsuffixed" \
    "tests/t104_for_local_integer_unsuffixed.bas" \
    --assert-pixel 10,12,FFFFFF \
    --assert-pixel 12,12,FFFFFF \
    --assert-pixel 14,12,FFFFFF \
    --assert-pixel 16,12,FFFFFF

run_case \
    "t106_array_param_byref_pixels" \
    "tests/t106_array_param_byref_pixels.bas" \
    --assert-pixel 10,12,FFFFFF \
    --assert-pixel 12,12,FFFFFF \
    --assert-pixel 14,12,FFFFFF \
    --assert-pixel 16,12,FFFFFF

run_case \
    "t134_pico_blocks_startup_geometry" \
    "tests/t134_pico_blocks_startup_geometry.bas" \
    --compare-framebuffer \
    --assert-pixel 166,166,FF0000 \
    --assert-pixel 135,308,FFFFFF \
    --assert-pixel 160,313,00FF00

run_case \
    "t144_triangle_scalar_forms" \
    "tests/t144_triangle_scalar_forms.bas" \
    --assert-pixel 20,10,FFFFFF \
    --assert-pixel 25,12,000000 \
    --assert-pixel 80,16,0000FF \
    --assert-pixel 110,20,000000 \
    --assert-pixel 150,18,FF00FF

run_case \
    "t145_triangle_array_forms" \
    "tests/t145_triangle_array_forms.bas" \
    --assert-pixel 20,40,FF0000 \
    --assert-pixel 50,40,00FF00 \
    --assert-pixel 50,45,00FFFF \
    --assert-pixel 80,45,FFFF00

run_case \
    "t147_font_text_defaults" \
    "tests/t147_font_text_defaults.bas" \
    --assert-pixel 16,12,FFFFFF \
    --assert-pixel 22,16,FFFFFF \
    --assert-pixel 42,12,FFFFFF \
    --assert-pixel 40,10,000000

run_case \
    "t148_polygon_scalar_forms" \
    "tests/t148_polygon_scalar_forms.bas" \
    --assert-pixel 10,10,000000 \
    --assert-pixel 20,10,FFFFFF \
    --assert-pixel 34,20,0000FF \
    --assert-pixel 30,34,0000FF

run_case \
    "t149_polygon_array_forms" \
    "tests/t149_polygon_array_forms.bas" \
    --assert-pixel 34,45,FF0000 \
    --assert-pixel 34,26,00FF00 \
    --assert-pixel 84,40,0000FF \
    --assert-pixel 72,28,FFFFFF

run_case \
    "t159_framebuffer_merge_basic" \
    "tests/t159_framebuffer_merge_basic.bas" \
    --assert-pixel 5,5,0000FF \
    --assert-pixel 25,25,00FF00 \
    --assert-pixel 40,32,FF0000 \
    --assert-pixel 75,25,00FF00

run_case \
    "t160_framebuffer_merge_running" \
    "tests/t160_framebuffer_merge_running.bas" \
    --assert-pixel 5,5,0000FF \
    --assert-pixel 20,20,00FF00 \
    --assert-pixel 35,28,FF0000 \
    --assert-pixel 85,20,00FF00

run_case \
    "t162_framebuffer_copy_display_roundtrip" \
    "tests/t162_framebuffer_copy_display_roundtrip.bas" \
    --assert-pixel 5,5,0000FF \
    --assert-pixel 20,20,0000FF

run_case \
    "t163_framebuffer_copy_buffer_roundtrip" \
    "tests/t163_framebuffer_copy_buffer_roundtrip.bas" \
    --assert-pixel 5,5,FF0000 \
    --assert-pixel 20,20,FF0000

run_case \
    "t164_framebuffer_copy_background_sync" \
    "tests/t164_framebuffer_copy_background_sync.bas" \
    --assert-pixel 5,5,00FF00 \
    --assert-pixel 20,20,00FF00
