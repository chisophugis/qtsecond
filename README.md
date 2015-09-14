A second brush with Qt

Most of the code can be executed by doing:
```
$ qmake && make && ./executable
```

The most interesting part (IMO) is in `video/`. It is a hand-written
video player that plays Y4M video using OpenGL (or rather Qt's very
well-done wrapper around it). It starts with the bytes of an mmap'd
Y4M file and puts video on the screen. The primary work is input file
parsing (done on CPU) and YUV->RGB conversion (done on GPU). There are
some remnants of a "Triangle" program that I incrementally grew to be
this video program.
