## TMaskCleaner for VapourSynth ##
A really simple mask cleaning plugin for AviSynth based on mt_hysteresis. It discards all areas of less than **length** pixels with values bigger or equal to **thresh**. Original plugin by tp7.

## Example usage ##
```tmc.TMaskCleaner(clip, length=5, thresh=235, fade=0)```

Original plugin works only with 8 bit input (and only Y plane). My port should support 10-16 bit as well (only plane 0 will be processed, 1 and 2 will be copied).

### License ###
This plugin is licensed under the [MIT license][mit_license]. Binaries are [GPL v2][gpl_v2] because if I understand licensing stuff right (please tell me if I don't) they must be.

[mit_license]: http://opensource.org/licenses/MIT
[gpl_v2]: http://www.gnu.org/licenses/gpl-2.0.html
