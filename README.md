# Hack the Matrix: ZZT arbitrary code execution

Hack the Matrix is a proof-of-concept arbitrary code execution exploit for ZZT
3.2. By using some out-of-bounds memory writes and copies, it's possible to get
ZZT to execute any x86 code you'd like. This exploit works on DOS ZZT 3.2 on
Dosbox, native, and Zeta. It is possible to defend against it by pre-filling
memory, but it's also possible to circumvent such a defense by slightly
changing the exploit.

## I'm reading this in a ZIP file: what repo is this file from?

https://github.com/kristomu/zzt-matrix

## What does the exploit do?

Upon execution, the exploit demonstrates that it's not constrained by ZZT by
doing a few things that ZZT can't. Some of the effect may be lost on Zeta,
though, which doesn't support the picture demo.

## What do the files do?

Since I wrote the source for my own use, the various programs aren't exactly
user friendly. Ask me if there's anything you're having trouble with their
usage.

### Countermeasures

- fill/: Code and binaries to fill memory with 0xFF. This makes exploitation much harder and will foil this version of the exploit, making it crash instead. The filenames contain the version number of ZZT they apply to.

### K-means vector quantization code and picture demo

- delta\_e\_2000.cc: Compute the CIEDE2000 color-difference function.
- kmeans.cc: Encode a ppm file (hard-coded in main()) into charset, palette, and image files that the exploit can use to display the picture in text mode.
- matrix_dithered.ppm: Default picture to be encoded, dithered for better reproduction.
- matrix_input.png: The original picture.

### Exploit code and binaries

- payload.asm: Flat Assembler source for the x86 exploit payload code.
- thematrx.zzt: Hack the Matrix ZZT file.

### Exploit engineering code and binaries

- prepare_objects.cc: Determine which Tick/Proc/Touch counts are good candidates
for overwriting (i.e. where the default values for the pointers resolve to
non-blockable tiles, so they can be overwritten by duplicators). Also hard-
coded to find indirect jumps for a particular destination. Uncomment the
commented-out for loop in main() after the A.2. comment to get more indirect
jump solutions.

### Memory dumps of the ZZT data segment

These are used by prepare_objects.

- mem\_dumps/unfilled\_dumps: Dump of the data segment with no preconditioning of memory, and specially prepared high-score file
- mem\_dumps/filled\_dumps: Dump of the data segment with memory filled with 0xFF beforehand. This is the least exploitable setup.
- mem\_dumps/highscore\_dumps: Dump of the data segment with memory filled, but also using a specially prepared high-score file to zero out some memory.
- mem\_dumps/thematrx.hi: The specially prepared high-score file.

### Format conversion tools

- render.cc: Turn .img, .chr, and .pal data into object files (.zoc) suitable
for insertion with literal-patched KevEdit. The program also reports where
bytes have to be overwritten by NUL (image data), or what XOR constant is used
for character data so that there are no null bytes to make trouble for KevEdit.

Note: KevEdit has to be patched to not turn LF or CR-LF into CR to properly
import the zoc files.
