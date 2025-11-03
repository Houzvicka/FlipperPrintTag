# OpenPrintTag Reader for Flipper Zero

A Flipper Zero application to read and write OpenPrintTag NFC tags used for 3D printing filament spools.

## About OpenPrintTag

OpenPrintTag is an open-source NFC tag standard for 3D printing materials, developed by Prusa Research. It stores material information like brand, type, color, and filament usage data on NFC tags attached to filament spools.

- **Specification**: https://specs.openprinttag.org/
- **GitHub Repository**: https://github.com/prusa3d/OpenPrintTag

## Features

- ✅ Read OpenPrintTag NFC tags (ISO15693/NFC-V)
- ✅ Parse NDEF + CBOR encoded data
- ✅ Display material information (brand, name, type, GTIN)
- ✅ Display auxiliary data (remaining filament, usage statistics)
- 🚧 Write/update auxiliary section (planned)
- 🚧 Create new OpenPrintTag tags (planned)

## Data Format

OpenPrintTag uses a hierarchical format:
- **NDEF Record**: MIME type `application/vnd.openprinttag`
- **CBOR Encoding**: Three sections encoded as CBOR maps
  - **Meta Section**: Region offsets and sizes
  - **Main Section**: Static material info (brand, material name, type, GTIN)
  - **Auxiliary Section**: Dynamic data (remaining length, used length, timestamp)

## Installation

### Prerequisites

- Flipper Zero with updated firmware
- [ufbt](https://github.com/flipperdevices/flipperzero-ufbt) or the full Flipper Zero firmware build environment

### Building with ufbt

```bash
# Clone this repository
git clone <your-repo-url>
cd FlipperPrintTag

# Build the application
ufbt

# Install to connected Flipper Zero
ufbt launch_app
```

### Building with full firmware

```bash
# Clone into the applications_user folder of your firmware
cd applications_user
git clone <your-repo-url> openprinttag

# Build firmware with this app included
cd ..
./fbt fap_openprinttag

# Copy to Flipper
./fbt launch_app APPSRC=openprinttag
```

## Usage

1. Open the **OpenPrintTag** app from the NFC category
2. Select **Read OpenPrintTag** from the menu
3. Place your Flipper Zero near an OpenPrintTag NFC tag (typically ISO15693/NFC-V)
4. View the parsed material information

## Project Structure

```
FlipperPrintTag/
├── application.fam              # App manifest
├── openprinttag.c               # Main app entry point
├── openprinttag_i.h             # Internal header
├── cbor_parser.h/c              # CBOR decoder
├── ndef_parser.c                # NDEF record parser
├── openprinttag_parser.c        # OpenPrintTag-specific parser
├── scenes/                      # Scene implementations
│   ├── openprinttag_scene_start.c
│   ├── openprinttag_scene_read.c
│   ├── openprinttag_scene_read_success.c
│   ├── openprinttag_scene_read_error.c
│   └── openprinttag_scene_display.c
└── assets/                      # Icons (to be added)
```

## Implementation Notes

### Current Limitations

1. **ISO15693 Reading**: The current implementation provides a framework but needs complete ISO15693 memory reading logic
2. **Icon**: A placeholder icon reference exists - you'll need to create `openprinttag.png` (10x10px)
3. **Write Support**: Writing to tags is not yet implemented
4. **Error Handling**: Basic error handling is in place but could be more robust

### Next Steps

To complete the application:

1. **Implement proper ISO15693 reading**:
   - Read tag memory blocks
   - Handle different tag types (ICODE SLIX, etc.)
   - Extract NDEF message from tag memory

2. **Add icon**: Create a 10x10px icon at `assets/openprinttag.png`

3. **Implement write functionality**:
   - Update auxiliary section with new filament usage data
   - Validate CBOR encoding before writing
   - Handle write protection

4. **Testing**: Test with real OpenPrintTag-enabled filament spools

## OpenPrintTag Field Reference

### Main Section Fields
- `1` - Brand name (string)
- `2` - Material name (string)
- `3` - Material class (enum)
- `4` - Material type (string)
- `10` - GTIN (number)

### Auxiliary Section Fields
- `1` - Remaining length in mm (number)
- `2` - Used length in mm (number)
- `10` - Last update timestamp (UNIX timestamp)

## Contributing

Contributions are welcome! Areas that need work:
- Complete ISO15693 tag reading implementation
- Add write/update functionality
- Create proper icon assets
- Add more comprehensive error handling
- Support for additional OpenPrintTag fields

## License

This project is provided as-is for educational and development purposes. The OpenPrintTag specification is open-source and free to implement.

## Resources

- [Flipper Zero Documentation](https://docs.flipper.net/)
- [OpenPrintTag Specification](https://specs.openprinttag.org/)
- [CBOR Specification](https://cbor.io/)
- [NFC Forum NDEF Specification](https://nfc-forum.org/)
