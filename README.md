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
Tabster opens a FIFO /tmp/tabsterPID, listening for commands:

 - new CMD %d
   opens a new tab, spawn CMD with %d replaced with the socket
 x cnew CMD %d
   open in next layer
 x bnew CMD %d
   open in background
 x bcnew CMD %d
   open in background in next layer
 - next
 - prev
 - close
 x treeclose
 - goto NUM
 - move NUM
 x attach NUM
 - hidetree
 - showtree

A environment variable "TABSTER_PID" is set, so a uzbl bind could look like this:

    bind tn = sh 'echo "new uzbl -s %d" > /tmp/tabster$TABSTER_PID'

A bit confusing: "%d" is replaced by tabster with the socket of the plug, while
"$TABSTER_PID" is replaced by the shell with the corresponding environment variable.

Contact
=======

Stefan Mark <mark at unserver dot de>

-- Stefan Mark

[1] https://github.com/jakeprobst/uzblstuff
[2] https://github.com/ThomasAdam/Gtkabber
[3] http://tools.suckless.org/tabbed
