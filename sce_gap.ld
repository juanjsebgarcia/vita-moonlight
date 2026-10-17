/* Keeps an 8 KB gap between the end of the code and the data segment.
 *
 * vita-elf-create appends its module tables (SCE data, a few KB) after the
 * text segment, but VitaSDK's armelf.x starts the data segment at the next
 * 64 KB boundary. When the code happens to end just short of a boundary,
 * there's no room, and it fails with "Cannot allocate N bytes for SCE data
 * at end of segment 0; segment 1 overlaps". This pads the code so it always
 * ends 8 KB before a boundary, at a cost of up to 64 KB of eboot. */
SECTIONS
{
  .sce_gap : { BYTE(0) . = ALIGN(. + 0x2000, 0x10000) - 0x2000; }
}
INSERT AFTER .eh_frame;
