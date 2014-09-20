Minimal Linux Bootloader
========================

Minimal Linux Bootloader, adapted/adopted from Sebastian Plotz.

This is a single stage x86 bootloader that can boot a single Linux kernel.
There is no support for initrd and only one kernel can be configured to boot at
any time.

While I have no plans for adding initrd support, I'm willing to consider it if
there is interest.

This bootloader is probably aimed at enthusiasts who are not afraid of breaking
things and know how the Linux boot process works. I am *not* responsible if you
render your machine unbootable with MLB.

MLB was originally written by Sebastian Plotz. I've changed the code very
slightly and wrote the Makefile and `mlbinstall`.

http://sebastian-plotz.blogspot.de/

Dependencies
------------

 - a C99 compiler (tested with GCC 4.7.3)
 - an x86 machine (duh)
 - make (tested with GNU Make 3.82)
 - nasm (tested with NASM 2.10.07)
 - xxd (tested with xxd 1.10)

Building
--------

Simply run `make`. You will get a binary called `mlbinstall` which you can use
to install the bootloader. If you want to, stick it in /bin or something.

Installing
----------

**Don't forget to backup your MBR!**

    dd if=/dev/sdXN of=/boot/mbr.bak bs=512 count=1

To install MLB, simply invoke

    mlbinstall <target> <kernel> <command line> [-vbr]

where:

 - `<target>` is where you want your bootloader installed (e.g. a file, a block
   device, whatever)
 - `<kernel>` is the kernel file to boot
 - `<command line>` is the command line to pass to the kernel - don't forget to
   pass `root=` as `mlbinstall` will not calculate the root device. Currently
   the command line cannot be longer than 40 characters (more just won't fit in
   the MBR).
 - `[-vbr]` will make `mlbinstall` ignore the space for the partition table,
   giving you more space for the command line. You can safely use it if you're
   installing to a Volume Boot Record (partition) and not the Master Boot
   Record, e.g. for using with another bootloader in multi-OS setups.

`mlbinstall` is quite noisy if it detects any problems. Restore your MBR from
backup, fix the problems, and try again. If it succeeds, it won't say anything.

Contact
-------

Email patches, pull requests, help requests to wiktor /at\ brodlo \dot/ net

Alternatively, send pull requests and/or open issues on github:
https://github.com/wiktor-b/mlb

Thanks!

- wiktor
