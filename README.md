## TMaskCleanerMod for VapourSynth ##
A really simple mask cleaning plugin for AviSynth based on mt_hysteresis. It discards all areas of less than **length** pixels with values bigger or equal to **thresh**. Original plugin by tp7. Ported to Vapoursynth by [Beatrice-Raws](https://github.com/Beatrice-Raws/VapourSynth-TMaskCleaner) and modded by dtlnor.

## Requirements
- Vapoursynth R54+ (API 4)
- System: 64bit
- CPU: AVX2+


## Example usage ##

### TMaskCleanerMod
```
core.tmcm.TMaskCleanerMod(clip clip, [int length, int thresh, int fade, bint binarize, int connectivity, bint keepless, int mode])
```
```tmcm.TMaskCleanerMod(clip, length=5, thresh=235, fade=0)```

Original plugin works only with 8 bit input (and only Y plane). Beatrice-Raws port should support 10-16 bit as well (only plane 0 will be processed, 1 and 2 will be copied).

This mod added some option inspire from opencv's CCL.  
Go to [Syntax and Parameters](#syntax-and-parameters) parts for details.

### GetCCLStats
```
core.tmcm.TMaskCleanerMod(clip clip, [int thresh, int connectivity])
```
```tmcm.GetCCLStats(clip, thresh=235)```

get FrameProps like:
```
_CCLStatNumLabels
_CCLStatAreas
_CCLStatLefts
_CCLStatTops
_CCLStatWidths
_CCLStatHeights
_CCLStatCentroids_x
_CCLStatCentroids_y
```
which should contains array (except `_CCLStatNumLabels`) if the input clip isn't a blank clip. Note that the first label is always the background label, so you will like to get the frame prop like this

```py
f.props.get('_CCLStatAreas', None)[1:]
```

## Syntax and Parameters ##

- **clip**  
    Input clip. Supports 8-16 bit Gray/YUV formats. For YUV input, only plane 0 is processed; planes 1 and 2 are copied.

- **length** = `5`  
    Discards connected areas that less than `length` pixels.

- **thresh** = `235`  
    Binarize threshold. Discards pixels with values less than `thresh` before applying the `length` filter. Range: 0-255 for 8-bit input, 0-65535 for 16-bit input.

- **fade** = `0`  
    Controls gradual transition from 0 to 255:
    - Areas below `length` will be zeroed
    - Areas within [`length`, `length`+`fade`] will be assigned values gradually from 0 to 255
    - Areas above `length`+`fade` will be set to 255

    This allows adaptive processing of area ranges.  
    For example, by transforming the mask from linear (0→255) distribution denoted here as `/` to:
    - `/\` distribution (0→128→0)  
        by using `.Expr("x 128 > 255 x - x ?")`  
        = if `x > 128` set `255-x`, else set `x`
    - `||` to grab some arbitrary range  
        by using `.Expr("x 118 > x 138 < & 255 0 ?")`  
        = only `x > 188 && x < 138` set `255`, else set `0`
    - non-linear `_/_` mask by smoothly amplifying the middle  
        by using `.Expr("x 128 > 255 x - x ? 100 - 28 / sqrt 256 *")`  
        = sqrt(`/\` - 100 / 28) * 255

- **binarize** = `false`  
    - `true`: Feed binarized result with `thresh` before applying `length` filter and fade multiplying
    - `false`: Retain original luma values before applying `length` filter and fade multiplying

- **connectivity** = `8`  
    Determines how connected components are identified:
    - `8`: Include all 8 surrounding pixels (including diagonals) in a 3×3 box
    - `4`: Include only adjacent pixels (top/bottom/left/right) in a 3×3 box

- **keepless** = `false`  
    - `false`
        - Areas **below** `length` will be zeroed
        - Areas within [`length`, `length`+`fade`] will be assigned values gradually from **0** to **255**
        - Areas **above** `length`+`fade` will be set to 255
    - `true`
        - Areas **above** `length` will be zeroed
        - Areas within [`length`-`fade`, `length`] will be assigned values gradually from **255** to **0**
        - Areas **below** `length`-`fade` will be set to 255

- **mode** = `0`  
    Determines how connected components are filtered.
    Note that the meaning of `length` and will change base on the modes.
    - `0`: Filter by **area** of component
    - `1`: Filter by **centroid x** position
    - `2`: Filter by **centroid y** position
    - `3`: Filter by **left** position of bounding box
    - `4`: Filter by **top** position of bounding box
    - `5`: Filter by **right** position of bounding box
    - `6`: Filter by **bottom** position of bounding box
    - `7`: Filter by **width** of bounding box
    - `8`: Filter by **height** of bounding box

### License ###
This plugin is licensed under the [MIT license][mit_license]. Binaries are [GPL v2][gpl_v2] because if I understand licensing stuff right (please tell me if I don't) they must be.

[mit_license]: http://opensource.org/licenses/MIT
[gpl_v2]: http://www.gnu.org/licenses/gpl-2.0.html
