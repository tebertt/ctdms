# ctdms

A C99 library for reading [NI TDMS](https://www.ni.com/en/support/documentation/supplemental/07/tdms-file-format-internal-structure.html) (Technical Data Management Streaming) files.

## Features

- Read TDMS file format versions 1.0 (4712) and 2.0 (4713)
- Three-level hierarchy: file → groups → channels
- All standard numeric types, strings, booleans, and timestamps
- Properties on file, group, and channel objects
- Multi-segment files with incremental metadata
- Interleaved and contiguous data layouts
- Cross-platform: Linux, macOS, Windows
- No external dependencies (pure C99)

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Options

| Option | Default | Description |
|---|---|---|
| `CTDMS_BUILD_SHARED` | `OFF` | Build as shared library |
| `CTDMS_BUILD_TESTS` | `ON` | Build test suite |
| `CTDMS_BUILD_EXAMPLES` | `ON` | Build example programs |

### Running Tests

```bash
cd build && ctest --output-on-failure
```

## Usage

```c
#include <ctdms/ctdms.h>
#include <stdio.h>

int main(void) {
    ctdms_file *file = ctdms_open("data.tdms");
    if (!file || ctdms_get_error(file)) {
        fprintf(stderr, "Error: %s\n", ctdms_get_error(file));
        ctdms_close(file);
        return 1;
    }

    int ngroups = ctdms_get_group_count(file);
    for (int gi = 0; gi < ngroups; gi++) {
        ctdms_group *group = ctdms_get_group(file, gi);
        printf("Group: %s\n", ctdms_group_get_name(group));

        int nch = ctdms_group_get_channel_count(group);
        for (int ci = 0; ci < nch; ci++) {
            ctdms_channel *ch = ctdms_group_get_channel(group, ci);
            printf("  Channel: %s (%lu values)\n",
                   ctdms_channel_get_name(ch),
                   (unsigned long)ctdms_channel_get_num_values(ch));
        }
    }

    ctdms_close(file);
    return 0;
}
```

## Integration with CMake

After installing (`cmake --install build`), use in your project:

```cmake
find_package(ctdms REQUIRED)
target_link_libraries(myapp PRIVATE ctdms::ctdms)
```

## License

GPL-3.0-or-later