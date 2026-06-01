**Overview**
 - **Purpose**: This document explains what each function in the `functions/` folder does, in simple terms. It focuses on how the code reads and writes BMP images and how each image operation works.

**`bmp_utils.h`**:
 - **Role**: low-level helpers for reading BMP metadata and opening input/output files.
 - **Key items**:
  -  **`bmp_image_info`**: a struct that stores BMP header bytes, optional extra header bytes, image width, height, bits-per-pixel, bytes per pixel, and padded row size.
  -  **`bmp_process_io`**: a small struct holding `FILE *` for the input image and output image path and handle.
 - **Main functions**:
  -  **`bmp_build_output_name(input_path, suffix, output_name, size)`**: builds a short output filename from the input path and an optional suffix. It strips directories and file extension, then appends `_suffix` if provided. This keeps output names readable and predictable.
  -  **`bmp_read_info(image, info)`**: reads the fixed 54-byte BMP header, extracts metadata (data offset, width, height, bits-per-pixel), computes `bytes_per_pixel` and `row_padded` (rows are padded to 4 bytes). It also reads any extra header bytes found between the fixed header and pixel data. Returns success/failure.
  -  **`bmp_write_header(outputImage, info)`**: writes back the same header and optional extra header into the output file so the output file keeps the same format and metadata as the input.
  -  **`bmp_open_process_io(input_path, name_output, bmp, io)`**: opens the input file (binary), opens/creates the output file under `/img/` using the name provided, writes the header to the output file, and seeks the input file to the pixel data offset. Returns 1 on success or 0 on error (and prints a helpful message). This function centralizes the boilerplate of file handling so the processing functions can focus on pixels.
  -  **`bmp_close_process_io(io)`**: closes any open FILE handles.

**`gris.h`**:
 - **Role**: convert a BMP image to grayscale row-by-row.
 - **How it works (simple)**:
  -  Open input and output files using `bmp_open_process_io`.
  -  Allocate a single buffer for one row (`row_padded` bytes).
  -  For each row: read the row from the input file, loop over pixels, compute a weighted average of R, G, B to get a gray value using the formula `(21*r + 72*g + 7*b)/100`, and write the gray value back into the three color channels for each pixel (B,G,R). Then write the whole row to the output file.
  -  The function preserves row padding and, if present, alpha channel (A fourth channel (in addition to R, G, B) that stores how opaque a pixel is. Values usually go 0 (fully transparent) → 255 (fully opaque).) is untouched because it just writes 3 bytes for color and leaves padding as-is.
  -  Error conditions (read/write failures or memory problems) are handled by printing messages and closing files.

**`inv_hz.h`** (horizontal inversion / mirror left-right):
 - **Role**: flip image horizontally. There are two variants: grayscale and full color.
 - **`inv_hz_gris`**:
  -  Reads each row and writes pixels reversed (mirror) into an output buffer, converting each pixel to grayscale on-the-fly using the same weighted average formula. Padding and alpha channel are handled: alpha is copied when present and padding is preserved.
  -  Works row-by-row; memory usage is small (two row buffers).
 - **`inv_hz_color`**:
  -  Same left-right mirror but preserves full color channels. It copies each channel into the reversed location and preserves padding.

**`inv_vt.h`** (vertical inversion / flip top-bottom):
 - **Role**: flip image vertically. Two variants: grayscale and color.
 - **`inv_vt_gris`**:
  -  Reads the whole image into memory as an array of row buffers (one buffer per row). Then it writes rows in reverse order to the output file, converting each pixel to grayscale while writing. This approach needs more memory (one buffer per row), but it makes vertical inversion trivial: just pick the appropriate row index in reverse.
  -  It preserves padding and alpha channel when present.
 - **`inv_vt_color`**:
  -  Similar to `inv_vt_gris`, but writes the full color rows in reverse order without converting to gray. It reads all rows into memory and then writes them back reversed.

**`desenfoque.h`** (blur / box blur) — detailed explanation:
 - **Files updated**: the `desenfoque` and `desenfoque_gris` functions were changed to use a 2D prefix-sum approach (also called a "summed-area table" or "integral image"). This section explains why and how in plain terms.

 - What is the goal?
  -  Apply a square box blur of a chosen kernel size (e.g., 3x3, 5x5) to every pixel. A box blur replaces each pixel with the average value of the pixels in a square neighborhood around it.

 - The naive way (what was done before):
  -  For each pixel you sum every neighbor inside the kernel and divide by the number of pixels. If the kernel is k×k, that is O(k^2) work per pixel. For large images and large kernels this becomes slow.

 - The prefix-sum (summed-area table) trick (what the code now does):
  -  Build a table where each cell (x,y) stores the sum of all pixel values in the rectangle from the image origin (0,0) to (x,y). This table can be computed with one pass over the image where each cell is the sum of the cell above plus the cell to the left, minus the cell above-left, plus the current pixel value. Because we store sums for whole rectangles, we can compute the sum of any axis-aligned rectangle in constant time using four lookups and a few adds/subtracts.
  -  Using this trick, the blur for each pixel becomes O(1) (constant time) instead of O(k^2). Building the summed-area table itself is O(W*H). So total work becomes O(W*H) + O(W*H) = O(W*H), independent of the kernel size.

 - How `desenfoque_gris` uses the approach (step-by-step):
  -  1) Read all rows into memory (each row has `row_padded` bytes). For grayscale, the code first converts each pixel into a single gray value using the weighted formula `(21*r + 72*g + 7*b)/100` and stores that gray back into the row buffer (so the RGB channels contain the same gray value).
  -  2) Allocate `sumGray`, an array sized `(H+1)*(W+1)` of 64-bit integers. The extra row and column (the +1) make rectangle sums simpler because an edge can be represented cleanly with zero values on row 0 and column 0.
  -  3) Fill `sumGray` so that `sumGray[(y+1)*(W+1)+(x+1)]` equals the sum of gray values in rectangle from (0,0) to (x,y) in the image. The formula used is:
  -     sum[y+1,x+1] = sum[y+1,x] + sum[y,x+1] - sum[y,x] + value(x,y)
  -  4) For each output pixel at (x,y), compute the coordinates of the blur square (clamped to the edges of the image). Let that square be (x1,y1) to (x2,y2). The sum of that square is:
  -     area_sum = sum[y2+1,x2+1] - sum[y1,x2+1] - sum[y2+1,x1] + sum[y1,x1]
  -    Then average = area_sum / area_size. Put that value into the output pixel's RGB channels.
  -  5) Write rows to the output file and free memory.

 - How `desenfoque` (color) uses the approach:
  -  Same idea but it builds three summed-area tables, one per color channel (`sumB`, `sumG`, `sumR`). Each table is filled from the raw channel values read from `input_rows`.
  -  For each pixel, the code calculates the box coordinates (x1,y1,x2,y2), queries each sum table with the four-lookups formula, divides by the area to get average for each channel, and writes averaged B,G,R into the output.

 - Important practical notes and corner cases:
  -  Memory: summed-area tables are stored as 64-bit integers for safety (to avoid overflow when summing many pixel values). The memory cost is approximately 8 bytes * (W+1) * (H+1) per table. For a 4000×3000 image, one table uses ~96 MB; three tables would use ~288 MB. This can be large on small machines. If memory is a problem, alternatives are:
   -   Use separable box blur (horizontal then vertical) while reusing row buffers (less memory but still not always minimal).
   -   Use an integer type with smaller width if you can guarantee no overflow (dangerous), or compute in tiles.
   -   Use an incremental sliding-window sum horizontally/vertically per row (O(W*H) but using O(W) memory).
  -  Borders: the code clamps the kernel rectangle at image edges instead of padding with zeros or reflecting. That means near the border the effective kernel area is smaller. That is visible as less blur near edges but is a common practical choice.
  -  Alpha and padding: the code preserves alpha (`bytes_per_pixel == 4` case) by copying the alpha channel from the input into the output unchanged. Row padding bytes are copied from input to output so the file remains a valid BMP.
  -  Performance: building each table and then applying the blur is very fast for large kernels because the per-pixel cost does not depend on the kernel size.

 - Example (very small, intuitive):
  -  If the image is 4×4 and kernel is 3 (k=1), for pixel (1,1) the neighborhood is the 3×3 square centered on it. With a summed-area table you look up four numbers and do 3 adds/subtracts to get the sum of those 9 cells, then divide by 9. No loop over 9 pixels is needed per output pixel.

**Common patterns across all functions**:
 -  They use `bmp_open_process_io` and `bmp_close_process_io` to manage files and headers.
 -  They try to preserve pixel layout and padding so that output files are valid BMPs.
 -  Error handling prints clear messages and frees memory when possible.

**Next steps / suggestions**:
 -  If you plan to run these on large images often, consider testing memory usage and, if needed, switching to a tiled approach or a separable blur to reduce peak memory.
 -  If you want, I can add a small test program or a CLI wrapper that exercises each function and reports runtime and memory usage for a given image and kernel size.

If you want this translated into Spanish or adapted to include short code snippets or diagrams, tell me which parts you prefer and I will update `explanation.md`.
