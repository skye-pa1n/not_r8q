# Linux kernel
============

There are several guides for kernel developers and users. These guides can
be rendered in a number of formats, like HTML and PDF. Please read
Documentation/admin-guide/README.rst first.

In order to build the documentation, use ``make htmldocs`` or
``make pdfdocs``.  The formatted documentation can also be read online at:

    https://www.kernel.org/doc/html/latest/

There are various text files in the Documentation/ subdirectory,
several of them using the Restructured Text markup notation.
See Documentation/00-INDEX for a list of what is contained in each file.

Please read the Documentation/process/changes.rst file, as it contains the
requirements for building and running the kernel, and information about
the problems which may result by upgrading your kernel.

# About not_kernel
Focused on **not**hing, not_kernel is supposed to be different.

The kernel parameters are highly customizable.
 
### KernelSU

+ KernelSU version: 11992
+ Hardcoded KernelSU version: 11992
+ No SusFS Support (for now)
+ Ksu-Legacy Manager Support
+ Manually hooked KSU (No KProbes)
  
## Compatibility

**`Android14 ROMS ONLY`**

aka.
- Lineage
- CrDroid
- RisingOS
- EvolutionX
- UN1CA (You won't have full support but notK also works there now)
