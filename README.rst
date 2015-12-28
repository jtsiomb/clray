Realtime GPU (OpenCL) polygon mesh raytracer
============================================

About
-----
Clray is an experimental realtime GPU raytracer with OpenCL. It renders polygon
mesh scenes loaded from obj files (with minor extensions to the material
description format), using a kd-tree constructed with the surface area heuristic
(SAH) for ray-test acceleration.

If you want to try clray, a few test scenes can be found at:
http://nuclear.mutantstargoat.com/sw/clray/clray_test_scenes.tar.gz

License
-------
Copyright (C) John Tsiombikas <nuclear@member.fsf.org>

Clray is free software; feel free to use, modify, and/or redistribute it, under
the terms of the GNU General Public License version 3, or (at your option) any
later version published by the Free Software Foundation. See COPYING for
details.

Material file format extensions
-------------------------------
Clray will happily read obj/mtl files as exported by most programs. However, the
obj material file format does not specify a reflectivity factor. For this reason
I've added a custom mtl command named "Nr" to specify reflectivity. Just add it
manually to any material you want to make reflective, followed by a number in
the range ``[0, 1]``. For instance a reflective red material with white specular
highlights could be defined as follows::

  newmtl mat_red_shiny
  Ka 1 0 0
  Kd 1 0 0
  Ks 0.8 0.8 0.8
  Ns 80
  Nr 0.75
