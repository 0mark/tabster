Tabster
========

This is supposed to be a universal replacement for uzbltreetab [1] in C.
Its based on Gtkabber [2], a GTK-replacement for tabbed [3]

Still very much in development. The main feature of uzbltreetab, the tree,
is not implemented yet. Doh!

Patches welcome, see the TODO file.

Dependencies
============

* GTK 2.X
* GLib

Operation
=========

The protocol-handling of Tabster is much likely to that of uzbltreetab,
except das commands spawning new tabs need to get a commandline of what
to spawn.
Tabster opens a FIFO /tmp/tabster (it already on the todo list to fix
that), listening for commands:

 - new CMD %d
   opens a new tab, spawn CMD with %d replaced with the socket
 - cnew CMD %d
   open in next layer
 - bnew CMD %d
   open in background
 - bcnew CMD %d
   open in background in next layer
 - next
 - prev
 - close
 - goto
 - move
 - attach
 - hidetree
 - sjowtree

Contact
=======

Stefan Mark <mark at unserver dot de>

-- Stefan Mark

[1] https://github.com/jakeprobst/uzblstuff
[2] https://github.com/ThomasAdam/Gtkabber
[3] http://tools.suckless.org/tabbed
