# Fix: xcb/xrender static linking dependency

## Summary

Fixes linker errors when building with static X11 libraries on Linux. When linking against `libX11.a` and `libXcursor.a`, the linker requires explicit dependencies on `libxcb` and `libXrender` which are not automatically resolved by meson's dependency detection.

## Changes

- Added `xcb_dep` and `xrender_dep` to `internal_deps` in `meson.build`
- These dependencies are optional and only applied when static X11 linking is detected

## Files Changed

| File | Change |
|------|--------|
| `meson.build` | +12 lines |

## Testing

Build verification:
```bash
meson setup build --wipe
meson compile -C build dosbox
```

## Related Issues

Fixes: Static X11 library linking errors on Linux systems

---

## Checklist

- [ ] Builds successfully on Linux with static X11 libraries
- [ ] No changes to existing functionality
- [ ] Follows project coding conventions
