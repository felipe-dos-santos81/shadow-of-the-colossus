# Vendored third-party headers

Single-file dependencies, checked in at pinned upstream revisions (fetched 2026-10-17).
Re-fetch with the commands in `docs/superpowers/plans/2026-10-17-s1-sdl-viewer.md` (Task 1,
Step 1); the SHA-256 sums below are the pins.

| Header | Upstream | Revision | URL |
|---|---|---|---|
| `cgltf.h` | [jkuhlmann/cgltf](https://github.com/jkuhlmann/cgltf) | `v1.15` | https://raw.githubusercontent.com/jkuhlmann/cgltf/v1.15/cgltf.h |
| `stb_image.h` | [nothings/stb](https://github.com/nothings/stb) | `2c980bb59875b0d32144a71867fbdebb2f77cd20` (stb_image v2.30) | https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h |
| `stb_image_write.h` | [nothings/stb](https://github.com/nothings/stb) | `2c980bb59875b0d32144a71867fbdebb2f77cd20` (stb_image_write v1.16) | https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image_write.h |
| `doctest.h` | [doctest/doctest](https://github.com/doctest/doctest) | `v2.4.11` | https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h |

SHA-256 (from `shasum -a 256 app/third_party/*.h`):

```
e378a21c084bf1f288bb799de827bb26906efb024255f1ecf1705ea13f11c6ec  app/third_party/cgltf.h
44faa038e9c3f9728efbda143748d01124ea0a27f4bf78f35a15d8fab2e039fb  app/third_party/doctest.h
594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3  app/third_party/stb_image.h
cbd5f0ad7a9cf4468affb36354a1d2338034f2c12473cf1a8e32053cb6914a05  app/third_party/stb_image_write.h
```
