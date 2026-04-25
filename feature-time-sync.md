# Feature Implementation: Permanent Host Time/Date Synchronization

## Implemented Features

- `[dosbox]` config section option changeable at runtime when idle: `hosttime=true/false`

## Implementation Details

**Platform-independent time sync using C++11 chrono**

The new implementation of BIOS_HostTimeSync() that uses:
- `std::chrono::system_clock::now()` for platform-independent time fetching
- `cross::localtime_r()` for thread-safe localtime conversion
- Includes both date AND time components

**Code locations:**
- `src/ints/bios.cpp` - `BIOS_HostTimeSync()` and `INT8_Handler()` modifications
- `src/dos/dos.cpp` - Config reading
- `include/dos_inc.h` - `dos.hosttime` flag

**Removed:**
- Preprocessor `#if DOSBOX_CLOCKSYNC` approach - replace with BIOS_HostTimeSync() call if hosttime flag is set
- Platform-specific `clock_gettime()` and `ftime()` conditional code
