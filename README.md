## Quadtree index implementation for ZIM files

This program is able to
1. parse the coordinates of all articles in a .zim file and store them in a quadree index file.
2. list all articles in a given area encoded in the index file.

To encode: Give the filename as argument, index will be written to stdout.
To search: Arguments: `<min_lat>` `<min_lon>` `<max_lat>` `<max_lon>`, the index file is expected at
           stdin latitudes and longitudes are given in decimal degrees.

The quadtree is encoded as a binary tree alternating in depth between latitudes and longitudes,
starting with latitudes.

The file starts right away with the root node being a latitude node. Each node is encoded as
follows:

Leaf node (detected by its first entry beng at most 10):
```
num_points          uint32 LE     4     number of points, always < 10
point1_lat          uint32 LE     4     latitude coordinate of the first point
point1_lon          uint32 LE     4     longitude coordinate of the first point
point2_lat          uint32 LE     4     latitude coordinate of the second point
point2_lon          uint32 LE     4     longitude coordinate of the second point
...
pointn_lat          uint32 LE     4     latitude coordinate of the nth point
pointn_lon          uint32 LE     4     longitude coordinate of the nth point
```

Inner node (where AXIS is either latitude or longitude, depending on the depth)
```
limit_value         uint32 LE     4
large_block_offset  uint32 LE     4     absolute file offset of the start of large_block
small_block         node          ?     inner or leaf node containing all points whose AXIS
                                        coordinate is less than limit_value
large_block         node          ?     inner or loaf node containing all points whose AXIS
                                        coordinate is at least limit_value
```

All coordinates are encoded as 32 bit little endian unsigned integers so that the respective range
is mapped linearly to the whole 32 bit integer spectrum. This means that the point (0, 0)
is encoded as (0x80000000, 0x80000000) and e.g. Paris, which is at 48.8567 / 2.3508 (lat / lon)
is encoded as (0xc57c2e7e, 0x81abf338). Note that the range of latitudes is only half the range
of longitudes, but they are both mapped to the full integer range.
