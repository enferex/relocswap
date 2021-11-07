relocswap: Swap the offset information of two ELF reloc entries.
================================================================
relocswap serves no sane purpose.  This tool identifies all dynamic relocation
entries of an ELF file.  Then, the tool will pseudo-randomly swap the two
relocation data.  Your program may still work, or may not.  Imagine what
absurdidies could arise if a printf becomes a syscall.  It's kinda like a 
self fuzzer, since relocs repesenting functions might be swapped your program
could end up calling a() thinking it was b(), and vice versa.

Usage
-----
See the -h option.

Building
--------
Run `make`

Contact
-------
Matt Davis: https://github.com/enferex
