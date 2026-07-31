set(LIBVTERM_INCLUDE_DIR "${THIRD_PARTY_DIR}/libvterm/include")
set(LIBVTERM_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/libvterm")

file(MAKE_DIRECTORY "${LIBVTERM_GENERATED_DIR}/encoding")
file(WRITE "${LIBVTERM_GENERATED_DIR}/encoding/DECdrawing.inc"
"static const struct StaticTableEncoding encoding_DECdrawing = {
  { NULL, &decode_table },
  {
    [0x60] = 0x25C6,
    [0x61] = 0x2592,
    [0x62] = 0x2409,
    [0x63] = 0x240C,
    [0x64] = 0x240D,
    [0x65] = 0x240A,
    [0x66] = 0x00B0,
    [0x67] = 0x00B1,
    [0x68] = 0x2424,
    [0x69] = 0x240B,
    [0x6a] = 0x2518,
    [0x6b] = 0x2510,
    [0x6c] = 0x250C,
    [0x6d] = 0x2514,
    [0x6e] = 0x253C,
    [0x6f] = 0x23BA,
    [0x70] = 0x23BB,
    [0x71] = 0x2500,
    [0x72] = 0x23BC,
    [0x73] = 0x23BD,
    [0x74] = 0x251C,
    [0x75] = 0x2524,
    [0x76] = 0x2534,
    [0x77] = 0x252C,
    [0x78] = 0x2502,
    [0x79] = 0x2A7D,
    [0x7a] = 0x2A7E,
    [0x7b] = 0x03C0,
    [0x7c] = 0x2260,
    [0x7d] = 0x00A3,
    [0x7e] = 0x00B7,
  }
};
")
file(WRITE "${LIBVTERM_GENERATED_DIR}/encoding/uk.inc"
"static const struct StaticTableEncoding encoding_uk = {
  { NULL, &decode_table },
  {
    [0x23] = 0x62e2,
  }
};
")

add_library(vterm STATIC
    "${THIRD_PARTY_DIR}/libvterm/src/encoding.c"
    "${THIRD_PARTY_DIR}/libvterm/src/keyboard.c"
    "${THIRD_PARTY_DIR}/libvterm/src/mouse.c"
    "${THIRD_PARTY_DIR}/libvterm/src/parser.c"
    "${THIRD_PARTY_DIR}/libvterm/src/pen.c"
    "${THIRD_PARTY_DIR}/libvterm/src/screen.c"
    "${THIRD_PARTY_DIR}/libvterm/src/state.c"
    "${THIRD_PARTY_DIR}/libvterm/src/unicode.c"
    "${THIRD_PARTY_DIR}/libvterm/src/vterm.c"
)

set_target_properties(vterm PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/lib/debug"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}/lib/release"
    ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_BINARY_DIR}/lib/release"
    ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_BINARY_DIR}/lib/release"
)

target_include_directories(vterm PUBLIC
    "${LIBVTERM_INCLUDE_DIR}"
    "${LIBVTERM_GENERATED_DIR}"
    "${THIRD_PARTY_DIR}/libvterm/src"
)

if(MSVC)
    target_compile_options(vterm PRIVATE /wd4018 /wd4244 /wd4267 /wd4996)
endif()
